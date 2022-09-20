#include <cmakeConfig.h>
#include <fuse_helper.h>
#include <fuse_req.h>
#include <fuse_kernel.h>
#include <fuse_reply.h>
#include <fuse_log.h>
#include <fuse_error.h>

#include <fcntl.h>
#include <dirent.h>

struct lo_inode
{
	struct lo_inode *next;  /* protected by lo->mutex */
	struct lo_inode *prev;  /* protected by lo->mutex */
	int fd;
	ino_t ino;
	dev_t dev;
	uint64_t refcount; 		/* protected by lo->mutex */
};

struct lo_data
{
	pthread_mutex_t mutex; // 循环遍历 lo_inode 的锁
	char *source;	   	   // 该文件系统被重定向的目标路径
	double timeout;
	struct lo_inode root; // 通过上面的 mutex 来控制访问
};

static const struct fuse_opt lo_opts[] = {
	DEFINE_FUSE_OPT("--source=%s", struct lo_data, source),
	FUSE_OPT_END
};

void fuse_passthrough_help(){
	printf("fuse passthrough options: \n");
	printf("    [--source=%%s]                source directory of the mounted fs (default=/),\n"
	       "                                 all vfs operations will be redirected to the source directory\n");
}

void free_lo_data(struct lo_data *data, int alloc)
{
	if (alloc)
		free(data->source);
	data->source = NULL;
}

static struct lo_data *lo_data(fuse_req_p req)
{
	return (struct lo_data *)req->se->userdata;
}

static struct lo_inode *lo_inode(fuse_req_p req, fuse_inode ino)
{
	if (ino == FUSE_ROOT_ID)
		return &lo_data(req)->root;
	else
		return (struct lo_inode *)(uintptr_t)ino;
}

static int lo_fd(fuse_req_p req, fuse_inode ino)
{
	return lo_inode(req, ino)->fd;
}

static struct lo_inode *lo_find(struct lo_data *lo, struct stat *st)
{
	struct lo_inode *p;
	struct lo_inode *ret = NULL;

	pthread_mutex_lock(&lo->mutex);
	for (p = lo->root.next; p != &lo->root; p = p->next)
	{
		if (p->ino == st->st_ino && p->dev == st->st_dev)
		{
			assert(p->refcount > 0);
			ret = p;
			ret->refcount++;
			break;
		}
	}
	pthread_mutex_unlock(&lo->mutex);
	return ret;
}

static void lo_destroy(void *userdata)
{
	struct lo_data *lo = (struct lo_data *)userdata;

	while (lo->root.next != &lo->root)
	{
		struct lo_inode *next = lo->root.next;
		lo->root.next = next->next;
		free(next);
	}
}

static int do_lookup(fuse_req_p req, fuse_inode parent, const char *name,
					 struct fuse_entry_param *e)
{
	int newfd;
	int res;
	int err;
	struct lo_data *lo = lo_data(req);
	struct lo_inode *inode;
	memset(e, 0, sizeof(*e));
	e->attr_timeout = lo->timeout;
	e->entry_timeout = lo->timeout;

	newfd = openat(lo_fd(req, parent), name, O_PATH | O_NOFOLLOW);
	if (newfd == -1)
		goto err_out;

	res = fstatat(newfd, "", &e->attr, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		goto err_out;

	inode = lo_find(lo_data(req), &e->attr);
	if (inode)
	{
		close(newfd);
		newfd = -1;
	}
	else
	{
		struct lo_inode *prev, *next;

		inode = calloc(1, sizeof(struct lo_inode));
		if (!inode)
			goto err_out;

		inode->refcount = 1;
		inode->fd = newfd;
		inode->ino = e->attr.st_ino;
		inode->dev = e->attr.st_dev;

		pthread_mutex_lock(&lo->mutex);
		prev = &lo->root;
		next = prev->next;
		next->prev = inode;
		inode->next = next;
		inode->prev = prev;
		prev->next = inode;
		pthread_mutex_unlock(&lo->mutex);
	}
	e->ino = (uintptr_t)inode;

	if (req->se->debug)
	{
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] lookup 0x%x/%s -> 0x%x\n",
				 (unsigned long long)parent, name, (unsigned long long)e->ino);
	}
	return 0;
err_out:
	err = errno;
	if (newfd != -1)
		close(newfd);
	return err;
}

void lo_lookup(fuse_req_p req, fuse_inode parent, const char *name)
{
	struct fuse_entry_param e;
	int err;
	if (req->se->debug)
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] lookup(parent=0x%x, name=%s)\n",
				 parent, name);

	err = do_lookup(req, parent, name, &e);
	if (err)
		send_reply_err(req, err);
	else
		send_reply_entry(req, &e);
}

static void forget_one(fuse_req_p req, fuse_inode ino, uint64_t nlookup)
{
	struct lo_data *lo = lo_data(req);
	struct lo_inode *inode = lo_inode(req, ino);

	if (inode == NULL)
		return;

	if (req->se->debug)
	{
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] forget 0x%x %llu -%llu\n",
				 (unsigned long long)ino,
				 (unsigned long long)inode->refcount,
				 (unsigned long long)nlookup);
	}

	pthread_mutex_lock(&lo->mutex);
	assert(inode->refcount >= nlookup);
	inode->refcount -= nlookup;
	if (!inode->refcount)
	{
		struct lo_inode *prev, *next;

		prev = inode->prev;
		next = inode->next;
		next->prev = prev;
		prev->next = next;

		pthread_mutex_unlock(&lo->mutex);
		close(inode->fd);
		free(inode);
	}
	else
	{
		pthread_mutex_unlock(&lo->mutex);
	}
}

static void lo_forget(fuse_req_p req, fuse_inode ino, uint64_t nlookup)
{
	forget_one(req, ino, nlookup);
	send_reply_none(req);
}

static void lo_rename(fuse_req_p req, fuse_inode parent, const char *name,
					  fuse_inode newparent, const char *newname)
{
	int res;

	res = renameat(lo_fd(req, parent), name,
				   lo_fd(req, newparent), newname);

	send_reply_err(req, res == -1 ? errno : 0);
}

static void lo_open(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)
{
	int fd;
	char buf[PATH_MAX];
	struct lo_data *lo = lo_data(req);

	if (req->se->debug)
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] open(ino=0x%x, flags=%d)\n",
				 ino, fi->flags);

	sprintf(buf, "/proc/self/fd/%i", lo_fd(req, ino));
	fd = open(buf, fi->flags & ~O_NOFOLLOW);
	if (fd == -1)
	{
		send_reply_err(req, errno);
		return;
	}

	fi->fh = fd;
	// if (lo->cache == CACHE_NEVER)
	fi->direct_io = 1;
	// else if (lo->cache == CACHE_ALWAYS)
	// fi->keep_cache = 1;
	send_reply_open(req, fi);
}

static void lo_create(fuse_req_p req, fuse_inode parent, const char *name,
					  mode_t mode, struct fuse_file_info *fi)
{
	int fd;
	struct lo_data *lo = lo_data(req);
	struct fuse_entry_param e;
	int err;

	if (req->se->debug)
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] create(parent=0x%x, name=%s)\n",
				 parent, name);

	fd = openat(lo_fd(req, parent), name,
				(fi->flags | O_CREAT) & ~O_NOFOLLOW, mode);

	if (fd == -1)
	{
		send_reply_err(req, errno);
		return;
	}

	fi->fh = fd;
	// if (lo->cache == CACHE_NEVER)
	fi->direct_io = 1;
	// else if (lo->cache == CACHE_ALWAYS)
	// fi->keep_cache = 1;

	err = do_lookup(req, parent, name, &e);
	if (err)
		send_reply_err(req, err);
	else
		send_reply_create(req, &e, fi);
}

static void lo_read(fuse_req_p req, fuse_inode ino, size_t size,
					off_t offset, struct fuse_file_info *fi)
{
#ifdef CRASH_TEST
	sleep(10);
#endif
	struct fuse_buf buf = {.mem = NULL, .size = size};

	if (req->se->debug)
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] read(ino=0x%x, size=%zd), "
								 "off=%lu)\n",
				 ino, size, (unsigned long)offset);

	buf.flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	buf.fd = fi->fh;
	buf.pos = offset;

	send_reply_read(req, &buf);
}

static void lo_write(fuse_req_p req, fuse_inode ino, char *buf,
					 size_t size, off_t off, struct fuse_file_info *fi)
{
#ifdef CRASH_TEST
	sleep(10);
#endif
	struct fuse_buf outbuf = {.mem = NULL, .size = size};
	outbuf.flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	outbuf.fd = fi->fh;
	outbuf.pos = off;

	struct fuse_buf inbuf = {.mem = buf, .size = size, .flags = 0};

	if (req->se->debug)
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] write(ino=0x%x, size=%zd, off=%lu)\n",
				 ino, size, (unsigned long)off);

	send_reply_write(req, &outbuf, &inbuf);
}

static void lo_unlink(fuse_req_p req, fuse_inode parent, const char *name)
{
	int res;

	res = unlinkat(lo_fd(req, parent), name, 0);

	send_reply_err(req, res == -1 ? errno : 0);
}

static void lo_release(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)
{
	(void)ino;

	close(fi->fh);
	send_reply_ok(req, NULL, 0);
}

static void lo_flush(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)
{
	int res;
	(void)ino;
	//  A successful close does not guarantee that the data has been
    //  successfully saved to disk, as the kernel uses the buffer cache
    //  to defer writes.  Typically, filesystems do not flush buffers
    //  when a file is closed.  If you need to be sure that the data is
    //  physically stored on the underlying disk, use fsync(2).  (It will
    //  depend on the disk hardware at this point.)
	res = close(dup(fi->fh));
	if (res == 0)
		send_reply_ok(req, NULL, 0);
	else
		send_reply_err(req, errno);
}

struct lo_dirp
{
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

static struct lo_dirp *lo_dirp(struct fuse_file_info *fi)
{
	return (struct lo_dirp *)(uintptr_t)fi->fh;
}

static void lo_opendir(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)
{
	int err;
	struct lo_data *lo = lo_data(req);
	struct lo_dirp *d;
	int fd;

	d = calloc(1, sizeof(struct lo_dirp));
	if (d == NULL)
		goto err_out;

	fd = openat(lo_fd(req, ino), ".", O_RDONLY);
	if (fd == -1)
		goto err_out;

	d->dp = fdopendir(fd);
	if (d->dp == NULL)
		goto err_out;

	d->offset = 0;
	d->entry = NULL;

	fi->fh = (uintptr_t)d;
	// if (lo->cache == CACHE_ALWAYS)
	// 	fi->cache_readdir = 1;
	send_reply_open(req, fi);
	return;

err_out:
	err = errno;
	if (d)
	{
		if (fd != -1)
			close(fd);
		free(d);
	}
	send_reply_err(req, err);
}

static void lo_mkdir(fuse_req_p req, fuse_inode parent, const char *name,
					 mode_t mode)
{
	int res;
	int err;
	struct lo_inode *dir = lo_inode(req, parent);
	struct fuse_entry_param e;
	res = mkdirat(dir->fd, name, mode | S_IFDIR);
	if (res < 0)
	{
		err = errno;
		goto err_out;
	}

	err = do_lookup(req, parent, name, &e);
	if (err)
		goto err_out;

	if (req->se->debug)
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] mkdir 0x%x/%s -> 0x%x\n",
				 (unsigned long long)parent, name, (unsigned long long)e.ino);

	send_reply_entry(req, &e);
	return;
err_out:
	send_reply_err(req, err);
}

static void lo_rmdir(fuse_req_p req, fuse_inode parent, const char *name)
{
	int res;

	res = unlinkat(lo_fd(req, parent), name, AT_REMOVEDIR);

	send_reply_err(req, res == -1 ? errno : 0);
}

size_t fuse_add_direntry(fuse_req_p req, char *buf, size_t bufsize,
						 const char *name, const struct stat *stbuf, off_t off)
{
	(void)req;
	size_t namelen;
	size_t entlen;
	size_t entlen_padded;
	struct fuse_dirent *dirent;

	namelen = strlen(name);
	entlen = FUSE_NAME_OFFSET + namelen;
	// 内存对齐，内存起始位置必须为 8 字节的整数倍，多余的补0
	entlen_padded = FUSE_DIRENT_ALIGN(entlen);

	if ((buf == NULL) || (entlen_padded > bufsize))
		return 0;

	dirent = (struct fuse_dirent *)buf;
	dirent->ino = stbuf->st_ino;
	dirent->off = off;
	dirent->namelen = namelen;
	dirent->type = (stbuf->st_mode & S_IFMT) >> 12;
	memcpy(dirent->name, name, namelen);
	memset(dirent->name + namelen, 0, entlen_padded - entlen);

	return entlen_padded;
}

static void lo_readdir(fuse_req_p req, fuse_inode ino, size_t size,
					   off_t offset, struct fuse_file_info *fi)
{
	struct lo_dirp *d = lo_dirp(fi);
	char *buf = NULL;
	char *p;
	size_t rem = size;
	int err;

	buf = calloc(1, size);
	if (buf == NULL)
	{
		goto err_out;
	}
	p = buf;

	if (d->offset != offset)
	{
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	errno = 0;
	size_t entsize;
	off_t nextoff;
	const char *name;
	d->entry = readdir(d->dp);
	while (d->entry)
	{
		nextoff = d->entry->d_off;
		name = d->entry->d_name;
		struct stat st = {
			.st_ino = d->entry->d_ino,
			.st_mode = d->entry->d_type << 12,
		};
		entsize = fuse_add_direntry(req, p, rem, name,
									&st, nextoff);
		if (entsize == 0)
			break;
		p += entsize;
		rem -= entsize;
		d->entry = readdir(d->dp);
		d->offset = nextoff;
	}
	if (errno)
		goto err_out;
	send_reply_ok(req, buf, size - rem);
	return;
err_out:
	err = errno;
	if (buf)
		free(buf);
	send_reply_err(req, err);
}

static void lo_releasedir(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)
{
	struct lo_dirp *d = lo_dirp(fi);
	(void)ino;
	closedir(d->dp);
	free(d);
	send_reply_ok(req, NULL, 0);
}

static void lo_getattr(fuse_req_p req, fuse_inode ino,
					   struct fuse_file_info *fi)
{
	int res;
	struct stat stbuf;
	struct lo_data *lo = lo_data(req);

	(void)fi;

	res = fstatat(lo_fd(req, ino), "", &stbuf, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
	if (res == 0)
		send_reply_attr(req, &stbuf, lo->timeout);
	else
		send_reply_err(req, errno);
}

static void lo_setattr(fuse_req_p req, fuse_inode ino, struct stat *attr,
					   int valid, struct fuse_file_info *fi)
{
	int err;
	char procname[64];
	struct lo_inode *inode = lo_inode(req, ino);
	int ifd = inode->fd;
	int res;

	if (valid & FATTR_MODE)
	{
		if (fi)
		{
			res = fchmod(fi->fh, attr->st_mode);
		}
		else
		{
			sprintf(procname, "/proc/self/fd/%i", ifd);
			res = chmod(procname, attr->st_mode);
		}
		if (res == -1)
			goto err_out;
	}
	if (valid & (FATTR_UID | FATTR_GID))
	{
		uid_t uid = (valid & FATTR_UID) ? attr->st_uid : (uid_t)-1;
		gid_t gid = (valid & FATTR_GID) ? attr->st_gid : (gid_t)-1;

		res = fchownat(ifd, "", uid, gid,
					   AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW);
		if (res == -1)
			goto err_out;
	}
	if (valid & FATTR_SIZE)
	{
		if (fi)
		{
			res = ftruncate(fi->fh, attr->st_size);
		}
		else
		{
			sprintf(procname, "/proc/self/fd/%i", ifd);
			res = truncate(procname, attr->st_size);
		}
		if (res == -1)
			goto err_out;
	}
	if (valid & (FATTR_ATIME | FATTR_MTIME))
	{
		struct timespec tv[2];

		tv[0].tv_sec = 0;
		tv[1].tv_sec = 0;
		tv[0].tv_nsec = UTIME_OMIT;
		tv[1].tv_nsec = UTIME_OMIT;

		if (valid & FATTR_ATIME_NOW)
			tv[0].tv_nsec = UTIME_NOW;
		else if (valid & FATTR_ATIME)
			tv[0] = attr->st_atim;

		if (valid & FATTR_MTIME_NOW)
			tv[1].tv_nsec = UTIME_NOW;
		else if (valid & FATTR_MTIME)
			tv[1] = attr->st_mtim;

		if (fi)
			res = futimens(fi->fh, tv);
		else
		{
			sprintf(procname, "/proc/self/fd/%i", ifd);
			res = utimensat(AT_FDCWD, procname, tv, 0);
		}
		if (res == -1)
			goto err_out;
	}

	return lo_getattr(req, ino, fi);

err_out:
	err = errno;
	send_reply_err(req, err);
}

static struct fuse_ops ops = {
	.destroy = lo_destroy,
	.lookup = lo_lookup,
	.forget = lo_forget,
	.rename = lo_rename,
	.open = lo_open,
	.create = lo_create,
	.read = lo_read,
	.write = lo_write,
	.unlink = lo_unlink,
	.release = lo_release,
	.flush = lo_flush,
	.opendir = lo_opendir,
	.mkdir = lo_mkdir,
	.rmdir = lo_rmdir,
	.readdir = lo_readdir,
	.releasedir = lo_releasedir,
	.getattr = lo_getattr,
	.setattr = lo_setattr
};

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	int res = -EBUILD;
	struct lo_data lo = {.timeout = 0};
	pthread_mutex_init(&lo.mutex, NULL);
	lo.root.next = lo.root.prev = &lo.root;
	lo.root.fd = -1;
	lo.source = NULL;
	int alloc = 0;

	if (fuse_opts_parse(&args, &lo, lo_opts) == -1)
		goto err_out;
	if (lo.source)
	{
		alloc = 1;
		struct stat stat;
		int err;

		err = lstat(lo.source, &stat);
		if (err == -1)
		{
			fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] failed to stat source (\"%s\"): %s\n",
					 lo.source, strerror(errno));
			goto err_out;
		}
		if (!S_ISDIR(stat.st_mode))
		{
			fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] source is not a directory\n");
			goto err_out;
		}
	}
	else
	{
		lo.source = "/";
	}
	lo.root.refcount = 2;
	lo.root.fd = open(lo.source, O_PATH);
	if (lo.root.fd == -1)
	{
		fuse_log(FUSE_LOG_ERR, "open(\"%s\", O_PATH): %s\n",
				 lo.source, strerror(errno));
		goto err_out;
	}

	res=fuse_normal_mode(&args,&ops,&lo,fuse_passthrough_help);

err_out:
	free_lo_data(&lo,alloc);
	free_fuse_args(&args);

	if(res==-EBUILD)
		exit(EXIT_SUCCESS);
	else
		exit(res>=0?EXIT_SUCCESS:EXIT_FAILURE);
}