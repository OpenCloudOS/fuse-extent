#include <fuse_operation.h>
#include <fuse_req.h>
#include <fuse_kernel.h>
#include <fuse_session.h>
#include <fuse_reply.h>

// FUSE Request Types Grouped by Semantics
// Group (#) 				| Request Types
// Special (3) 				| init, destroy, interrupt
// Metadata (14) 			| lookup, forget, batch_forget, create, unlink, link, rename,
// 					  		  rename2, open, release, statfs, fsync, flush, access
// Data (2) 				| read, write
// Attributes (2) 			| getattr, setattr
// Extended Attributes (4)  | setxattr, getxattr, listxattr, removexattr
// Symlinks (2) 			| symlink, readlink
// Directory (7) 			| mkdir, rmdir, opendir, releasedir, readdir, readdirplus, fsyncdir
// Locking (3) 				| getlk, setlk, setlkw
// Misc (6) 				| bmap, fallocate, mknod, ioctl, poll, notify_reply

#define ENTER_ONCE(req, outlabel)                 \
	{                                             \
		if (atomic_flag_test_and_set(&req->used)) \
			goto outlabel;                        \
	}

#define OUT \
	out:    \
	return;

#define PARAM(inarg) (((char *)(inarg)) + sizeof(*(inarg)))

static struct fuse_session *se_instance = NULL;

void fuse_session_set_ptr(void *ptr)
{
	se_instance = (struct fuse_session *)ptr;
}

static void fuse_session_save(struct fuse_session *se)
{
	if (se_instance)
	{
		se_instance->conn = se->conn;
		se_instance->inited = se->inited;
		se_instance->destroyed=se->destroyed;
	}
}

void fuse_session_recovery(struct fuse_session *se)
{
	if (se_instance)
	{
		se->conn = se_instance->conn;
		se->inited = se_instance->inited;
		se->destroyed=se_instance->destroyed;
	}
}

void do_init(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_init_in *arg = (struct fuse_init_in *)inarg;
	struct fuse_init_out outarg;
	struct fuse_session *se = req->se;

	(void)nodeid;

	if (se->debug)
	{
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] INIT: %u.%u\n", arg->major, arg->minor);
		if (arg->major == FUSE_KERNEL_VERSION && arg->minor >= 6)
		{
			fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] flags=0x%08x\n", arg->flags);
			fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] max_readahead=0x%08x\n",
					 arg->max_readahead);
		}
	}

	se->conn.proto_major = arg->major;
	se->conn.proto_minor = arg->minor;
	se->conn.capable = arg->flags;
	se->conn.want = 0;

	memset(&outarg, 0, sizeof(outarg));
	outarg.major = FUSE_KERNEL_VERSION;
	outarg.minor = FUSE_KERNEL_MINOR_VERSION;

	// 内核版本小于当前使用的头文件版本的话则退出
	if (arg->major < FUSE_KERNEL_VERSION)
	{
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: unsupported protocol version: %u.%u\n",
				 arg->major, arg->minor);
		send_reply_err(req, EPROTO);
		se->error = -EPROTO;
		se->exited = 1;
		return;
	}

	// 内核版本大于当前使用的头文件版本的话
	// 请求内核重新发送一个当前版本的 init 请求
	if (arg->major > FUSE_KERNEL_VERSION)
	{
		send_reply_ok(req, &outarg, sizeof(outarg));
		return;
	}

	// 设置 se->bufsize 以及 se->conn.max_readahead
	size_t bufsize = se->bufsize;
	if (arg->minor >= 6)
	{
		if (arg->max_readahead < se->conn.max_readahead)
			se->conn.max_readahead = arg->max_readahead;
		if (!(se->conn.capable & FUSE_MAX_PAGES))
		{
			size_t max_bufsize = FUSE_DEFAULT_MAX_PAGES_PER_REQ * getpagesize() + FUSE_BUFFER_HEADER_SIZE;
			if (se->bufsize > max_bufsize)
			{
				se->bufsize = max_bufsize;
			}
		}
	}
	else
	{
		se->conn.max_readahead = 0;
	}
	if (bufsize < FUSE_MIN_READ_BUFFER)
	{
		fuse_log(FUSE_LOG_WARNING, "[FUSE_LOG_WARNING] fuse: buffer size too small: %zu\n",
				 bufsize);
		bufsize = FUSE_MIN_READ_BUFFER;
	}
	se->bufsize = bufsize;

	// 如果 max_write 比 buffer 里面装的主体内容还要大
	// 那么多余的 max_write 是用不到的，所以将 max_write 设成和 buffer 里面装的主体内容一样大
	if (se->conn.max_write > bufsize - FUSE_BUFFER_HEADER_SIZE)
		se->conn.max_write = bufsize - FUSE_BUFFER_HEADER_SIZE;

	// 如果 max_write 比 buffer 里面装的主体内容还要小
	// 那么需要将 buffer 主体的大小设成和 max_write 一样大
	if (se->conn.max_write < bufsize - FUSE_BUFFER_HEADER_SIZE)
		se->bufsize = se->conn.max_write + FUSE_BUFFER_HEADER_SIZE;

	// 设置 se->conn.max_background 和 se->conn.congestion_threshold
	if (se->conn.proto_minor >= 13)
	{
		if (se->conn.max_background >= (1 << 16))
			se->conn.max_background = (1 << 16) - 1;
		if (se->conn.congestion_threshold > se->conn.max_background)
			se->conn.congestion_threshold = se->conn.max_background;
		if (!se->conn.congestion_threshold)
		{
			se->conn.congestion_threshold =
				se->conn.max_background * 3 / 4;
		}
		outarg.max_background = se->conn.max_background;
		outarg.congestion_threshold = se->conn.congestion_threshold;
	}

	se->conn.time_gran = 1;

	se->inited = 1;
	if (se->ops.init)
		se->ops.init(se->userdata, &se->conn);

	if (se->conn.want & (~se->conn.capable))
	{
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: filesystem requested capabilities "
							   "0x%x that are not supported by kernel, aborting.\n",
				 se->conn.want & (~se->conn.capable));
		send_reply_err(req, EPROTO);
		se->error = -EPROTO;
		se->exited = 1;
		return;
	}

	if (arg->flags & FUSE_MAX_PAGES)
	{
		outarg.flags |= FUSE_MAX_PAGES;
		outarg.max_pages = (se->conn.max_write - 1) / getpagesize() + 1;
	}
	outarg.flags |= FUSE_BIG_WRITES;
	outarg.max_write = se->conn.max_write;
	outarg.max_readahead = se->conn.max_readahead;
	if (se->conn.proto_minor >= 23)
		outarg.time_gran = se->conn.time_gran;

	if (se->debug)
	{
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] INIT: %u.%u\n", outarg.major, outarg.minor);
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] flags=0x%08x\n", outarg.flags);
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] max_readahead=0x%08x\n",
				 outarg.max_readahead);
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] max_write=0x%08x\n", outarg.max_write);
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] max_background=%i\n",
				 outarg.max_background);
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] congestion_threshold=%i\n",
				 outarg.congestion_threshold);
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] time_gran=%u\n",
				 outarg.time_gran);
	}

	size_t outargsize = sizeof(outarg);
	if (arg->minor < 5)
		outargsize = FUSE_COMPAT_INIT_OUT_SIZE;
	else if (arg->minor < 23)
		outargsize = FUSE_COMPAT_22_INIT_OUT_SIZE;

	fuse_session_save(se);
	send_reply_ok(req, &outarg, outargsize);
}

static void do_destroy(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_session *se = req->se;

	(void)nodeid;
	(void)inarg;

	se->destroyed = 1;
	if (se->ops.destroy)
		se->ops.destroy(se->userdata);

	fuse_session_save(se);
	send_reply_ok(req, NULL, 0);
}

static int find_interrupted(struct fuse_session *se, fuse_req_p req)
{
	fuse_req_p curr;

	for (curr = se->req_list.next; curr != &se->req_list; curr = curr->next)
	{
		if (curr->unique == req->interrupted_id)
		{
			// 找到之后需要确保这个请求当前未被处理，否则无法打断
			ENTER_ONCE(curr, out);
			pthread_mutex_unlock(&se->lock);
			send_reply_none(curr);
			pthread_mutex_lock(&se->lock);
		out:
			return 1;
		}
	}
	for (curr = se->int_list.next; curr != &se->int_list; curr = curr->next)
	{
		if (curr->interrupted_id == req->interrupted_id)
			return 1;
	}
	return 0;
}

// do_interrupt 请求仅仅实现了当对应的请求还没有开始被处理时
// 如果收到 interrupt 请求，那么就移除这个请求
// 如果这个请求已经在处理过程中，那么仅仅设置这个请求的 interrupted 标记为 1
static void do_interrupt(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_interrupt_in *arg = (struct fuse_interrupt_in *)inarg;
	struct fuse_session *se = req->se;

	(void)nodeid;
	if (se->debug)
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] INTERRUPT: %llu\n",
				 (unsigned long long)arg->unique);

	req->interrupted_id = arg->unique;

	pthread_mutex_lock(&se->lock);
	// 如果找到需要打断的请求，则从请求列表中删除（如果有的话），并释放这个 int 请求
	// 如果没有找到，则将这个 int 请求加入列表
	if (find_interrupted(se, req))
		free(req);
	else
		list_add_item(req, se->int_list);
	pthread_mutex_unlock(&se->lock);
}

static void do_lookup(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	char *name = (char *)inarg;

	ENTER_ONCE(req, out);
	if (req->se->ops.lookup)
		req->se->ops.lookup(req, nodeid, name);
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void do_create(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_create_in *arg = (struct fuse_create_in *)inarg;
	struct fuse_file_info fi;
	char *name = PARAM(arg);

	memset(&fi, 0, sizeof(fi));
	fi.flags = arg->flags;

	if (req->se->conn.proto_minor >= 12)
		req->ctx.umask = arg->umask;
	else
		name = (char *)inarg + sizeof(struct fuse_open_in);

	ENTER_ONCE(req, out);
	if (req->se->ops.create)
	{
		req->se->ops.create(req, nodeid, name, arg->mode, &fi);
	}
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void do_open(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_open_in *arg = (struct fuse_open_in *)inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.flags = arg->flags;

	ENTER_ONCE(req, out);
	if (req->se->ops.open)
		req->se->ops.open(req, nodeid, &fi);
	else
		send_reply_open(req, &fi);
	OUT
}

static void do_read(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_read_in *arg = (struct fuse_read_in *)inarg;

	ENTER_ONCE(req, out);
	if (req->se->ops.read)
	{
		struct fuse_file_info fi;

		memset(&fi, 0, sizeof(fi));
		fi.fh = arg->fh;
		if (req->se->conn.proto_minor >= 9)
		{
			fi.lock_owner = arg->lock_owner;
			fi.flags = arg->flags;
		}
		req->se->ops.read(req, nodeid, arg->size, arg->offset, &fi);
	}
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void do_write(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_write_in *arg = (struct fuse_write_in *)inarg;
	struct fuse_file_info fi;
	char *param;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;
	fi.writepage = (arg->write_flags & FUSE_WRITE_CACHE) != 0;

	if (req->se->conn.proto_minor >= 9)
	{
		fi.lock_owner = arg->lock_owner;
		fi.flags = arg->flags;
		param = PARAM(arg);
	}
	else
	{
		param = ((char *)arg) + FUSE_COMPAT_WRITE_IN_SIZE;
	}

	ENTER_ONCE(req, out);
	if (req->se->ops.write)
	{
		req->se->ops.write(req, nodeid, param, arg->size,
						   arg->offset, &fi);
	}
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void do_unlink(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	char *name = (char *)inarg;

	ENTER_ONCE(req, out);
	if (req->se->ops.unlink)
		req->se->ops.unlink(req, nodeid, name);
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void do_release(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_release_in *arg = (struct fuse_release_in *)inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.flags = arg->flags;
	fi.fh = arg->fh;
	if (req->se->conn.proto_minor >= 8)
	{
		fi.flush = (arg->release_flags & FUSE_RELEASE_FLUSH) ? 1 : 0;
		fi.lock_owner = arg->lock_owner;
	}
	if (arg->release_flags & FUSE_RELEASE_FLOCK_UNLOCK)
	{
		fi.flock_release = 1;
		fi.lock_owner = arg->lock_owner;
	}

	ENTER_ONCE(req, out);
	if (req->se->ops.release)
		req->se->ops.release(req, nodeid, &fi);
	else
		send_reply_err(req, 0);
	OUT
}

static void do_mkdir(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_mkdir_in *arg = (struct fuse_mkdir_in *)inarg;

	if (req->se->conn.proto_minor >= 12)
		req->ctx.umask = arg->umask;

	ENTER_ONCE(req, out);
	if (req->se->ops.mkdir)
		req->se->ops.mkdir(req, nodeid, PARAM(arg), arg->mode);
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void do_opendir(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_open_in *arg = (struct fuse_open_in *)inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.flags = arg->flags;

	ENTER_ONCE(req, out);
	if (req->se->ops.opendir)
		req->se->ops.opendir(req, nodeid, &fi);
	else
		send_reply_open(req, &fi);
	OUT
}

static void do_readdir(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_read_in *arg = (struct fuse_read_in *)inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;

	ENTER_ONCE(req, out);
	if (req->se->ops.readdir)
		req->se->ops.readdir(req, nodeid, arg->size, arg->offset, &fi);
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void do_rmdir(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	char *name = (char *)inarg;

	ENTER_ONCE(req, out);
	if (req->se->ops.rmdir)
		req->se->ops.rmdir(req, nodeid, name);
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void do_releasedir(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_release_in *arg = (struct fuse_release_in *)inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.flags = arg->flags;
	fi.fh = arg->fh;

	ENTER_ONCE(req, out);
	if (req->se->ops.releasedir)
		req->se->ops.releasedir(req, nodeid, &fi);
	else
		send_reply_err(req, 0);
	OUT
}

static void do_rename(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_rename_in *arg = (struct fuse_rename_in *)inarg;
	char *oldname = PARAM(arg);
	char *newname = oldname + strlen(oldname) + 1;

	ENTER_ONCE(req, out);
	if (req->se->ops.rename)
		req->se->ops.rename(req, nodeid, oldname, arg->newdir, newname);
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void do_forget(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_forget_in *arg = (struct fuse_forget_in *)inarg;

	ENTER_ONCE(req, out);
	if (req->se->ops.forget)
		req->se->ops.forget(req, nodeid, arg->nlookup);
	else
		send_reply_none(req);
	OUT
}

static void do_getattr(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	ENTER_ONCE(req, out);
	if (req->se->ops.getattr)
	{
		struct fuse_file_info *fip = NULL;
		struct fuse_file_info fi;

		if (req->se->conn.proto_minor >= 9)
		{
			struct fuse_getattr_in *arg = (struct fuse_getattr_in *)inarg;

			if (arg->getattr_flags & FUSE_GETATTR_FH)
			{
				memset(&fi, 0, sizeof(fi));
				fi.fh = arg->fh;
				fip = &fi;
			}
		}
		req->se->ops.getattr(req, nodeid, fip);
	}
	else
		send_reply_err(req, ENOSYS);
	OUT
}

static void convert_attr(struct stat *stbuf, const struct fuse_setattr_in *attr)
{
	stbuf->st_mode = attr->mode;
	stbuf->st_uid = attr->uid;
	stbuf->st_gid = attr->gid;
	stbuf->st_size = attr->size;
	stbuf->st_atime = attr->atime;
	stbuf->st_mtime = attr->mtime;
	stbuf->st_ctime = attr->ctime;
	stbuf->st_atim.tv_nsec = attr->atimensec;
	stbuf->st_mtim.tv_nsec = attr->mtimensec;
	stbuf->st_ctim.tv_nsec = attr->ctimensec;
}

static void do_setattr(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_setattr_in *arg = (struct fuse_setattr_in *)inarg;

	if (req->se->ops.setattr)
	{
		struct fuse_file_info *fi = NULL;
		struct fuse_file_info fi_store;
		struct stat stbuf;
		memset(&stbuf, 0, sizeof(stbuf));
		convert_attr(&stbuf, arg);
		if (arg->valid & FATTR_FH)
		{
			arg->valid &= ~FATTR_FH;
			memset(&fi_store, 0, sizeof(fi_store));
			fi = &fi_store;
			fi->fh = arg->fh;
		}
		arg->valid &=
			FATTR_MODE |
			FATTR_UID |
			FATTR_GID |
			FATTR_SIZE |
			FATTR_ATIME |
			FATTR_MTIME |
			FATTR_ATIME_NOW |
			FATTR_MTIME_NOW |
			FATTR_CTIME;
		req->se->ops.setattr(req, nodeid, &stbuf, arg->valid, fi);
	}
	else
		send_reply_err(req, ENOSYS);
}

static void do_flush(fuse_req_p req, fuse_inode nodeid, const void *inarg)
{
	struct fuse_flush_in *arg = (struct fuse_flush_in *)inarg;
	struct fuse_file_info fi;

	memset(&fi, 0, sizeof(fi));
	fi.fh = arg->fh;
	fi.flush = 1;
	if (req->se->conn.proto_minor >= 7)
		fi.lock_owner = arg->lock_owner;

	if (req->se->ops.flush)
		req->se->ops.flush(req, nodeid, &fi);
	else
		send_reply_err(req, ENOSYS);
}

static struct
{
	void (*func)(fuse_req_p, fuse_inode, const void *);
	const char *name;
} fuse_ops[] = {
	[FUSE_LOOKUP] = {do_lookup, "LOOKUP"},
	[FUSE_FORGET] = {do_forget, "FORGET"},
	[FUSE_GETATTR] = {do_getattr, "GETATTR"},
	[FUSE_SETATTR] = {do_setattr, "SETATTR"},
	[FUSE_READLINK] = {NULL, "READLINK"}, // No Implementation
	[FUSE_SYMLINK] = {NULL, "SYMLINK"},	  // No Implementation
	[FUSE_MKNOD] = {NULL, "MKNOD"},		  // No Implementation
	[FUSE_MKDIR] = {do_mkdir, "MKDIR"},
	[FUSE_UNLINK] = {do_unlink, "UNLINK"},
	[FUSE_RMDIR] = {do_rmdir, "RMDIR"},
	[FUSE_RENAME] = {do_rename, "RENAME"},
	[FUSE_LINK] = {NULL, "LINK"}, // No Implementation
	[FUSE_OPEN] = {do_open, "OPEN"},
	[FUSE_READ] = {do_read, "READ"},
	[FUSE_WRITE] = {do_write, "WRITE"},
	[FUSE_STATFS] = {NULL, "STATFS"}, // No Implementation TEMP
	[FUSE_RELEASE] = {do_release, "RELEASE"},
	[FUSE_FSYNC] = {NULL, "FSYNC"},				// No Implementation TEMP
	[FUSE_SETXATTR] = {NULL, "SETXATTR"},		// No Implementation
	[FUSE_GETXATTR] = {NULL, "GETXATTR"},		// No Implementation
	[FUSE_LISTXATTR] = {NULL, "LISTXATTR"},		// No Implementation
	[FUSE_REMOVEXATTR] = {NULL, "REMOVEXATTR"}, // No Implementation
	[FUSE_FLUSH] = {do_flush, "FLUSH"},
	[FUSE_INIT] = {do_init, "INIT"},
	[FUSE_OPENDIR] = {do_opendir, "OPENDIR"},
	[FUSE_READDIR] = {do_readdir, "READDIR"},
	[FUSE_RELEASEDIR] = {do_releasedir, "RELEASEDIR"},
	[FUSE_FSYNCDIR] = {NULL, "FSYNCDIR"}, // No Implementation TEMP
	[FUSE_GETLK] = {NULL, "GETLK"},		  // No Implementation TEMP
	[FUSE_SETLK] = {NULL, "SETLK"},		  // No Implementation TEMP
	[FUSE_SETLKW] = {NULL, "SETLKW"},	  // No Implementation TEMP
	[FUSE_ACCESS] = {NULL, "ACCESS"},	  // No Implementation TEMP
	[FUSE_CREATE] = {do_create, "CREATE"},
	[FUSE_INTERRUPT] = {do_interrupt, "INTERRUPT"},
	[FUSE_BMAP] = {NULL, "BMAP"},			// No Implementation
	[FUSE_IOCTL] = {NULL, "IOCTL"},			// No Implementation
	[FUSE_POLL] = {NULL, "POLL"},			// No Implementation
	[FUSE_FALLOCATE] = {NULL, "FALLOCATE"}, // No Implementation
	[FUSE_DESTROY] = {do_destroy, "DESTROY"},
	[FUSE_NOTIFY_REPLY] = {NULL, "NOTIFY_REPLY"},		 // No Implementation TEMP
	[FUSE_BATCH_FORGET] = {NULL, "BATCH_FORGET"},		 // No Implementation
	[FUSE_READDIRPLUS] = {NULL, "READDIRPLUS"},			 // No Implementation
	[FUSE_RENAME2] = {NULL, "RENAME2"},					 // No Implementation
	[FUSE_LSEEK] = {NULL, "LSEEK"},						 // No Implementation TEMP
	[FUSE_COPY_FILE_RANGE] = {NULL, "COPY_FILE_RANGE"}}; // No Implementation

#define FUSE_MAXOP (sizeof(fuse_ops) / sizeof(fuse_ops[0]))

static const char *opname(enum fuse_opcode opcode)
{
	if (opcode >= FUSE_MAXOP || !fuse_ops[opcode].name)
		return "???";
	else
		return fuse_ops[opcode].name;
}