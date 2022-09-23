#include <fuse_reply.h>

static size_t iov_length(const struct iovec *iov, size_t count)
{
	size_t seg;
	size_t ret = 0;

	for (seg = 0; seg < count; seg++)
		ret += iov[seg].iov_len;
	return ret;
}


int fuse_send_iov_msg(struct fuse_session *se, int clonefd,
							 struct iovec *iov, int count)
{
	struct fuse_out_header *out = iov[0].iov_base;
	ssize_t res;
	int err;

	assert(se != NULL);
	out->len = iov_length(iov, count);
	if (se->debug)
	{
		if (out->unique == 0)
		{
			// 通知消息没有对应的请求ID
			fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] NOTIFY: code=%d length=%u\n",
					 out->error, out->len);
		}
		else if (out->error)
		{
			// 请求处理错误消息
			fuse_log(FUSE_LOG_DEBUG,
					 "[FUSE_LOG_DEBUG] unique: %llu, error: %i (%s), outsize: %i\n",
					 (unsigned long long)out->unique, out->error,
					 strerror(-out->error), out->len);
		}
		else
		{
			// 请求处理成功消息
			fuse_log(FUSE_LOG_DEBUG,
					 "[FUSE_LOG_DEBUG] unique: %llu, success, outsize: %i\n",
					 (unsigned long long)out->unique, out->len);
		}
	}

restart:
	res = writev(clonefd==-1 ? se->fd : clonefd, iov, count);
	err = errno;
	if (se->exited)
	{
		return 0;
	}
	if (res == -1)
	{
		if (err == EINTR)
		{
			goto restart;
		}

		if (err == ENODEV)
		{
			se->exited = 1;
			return 0;
		}

		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: writing device: %s\n", strerror(errno));
		return -err;
	}

	return 0;
}

int send_iov_reply(fuse_req_p req, int error,
						  const void *arg, size_t argsize)
{
	struct iovec iov[2];
	int count = 1;

	struct fuse_out_header out;
	out.unique = req->unique;
	out.error = error;
	iov[0].iov_base = &out;
	iov[0].iov_len = sizeof(struct fuse_out_header);
	if (argsize)
	{
		iov[1].iov_base = (void *)arg;
		iov[1].iov_len = argsize;
		count++;
	}
	int res=fuse_send_iov_msg(req->se, req->fd, iov, count);
	pthread_mutex_lock(&req->se->lock);
	list_del_item(struct fuse_req,req);
	pthread_mutex_unlock(&req->se->lock);
	free(req);
	return res;
}

inline void send_reply_none(fuse_req_p req)
{
	pthread_mutex_lock(&req->se->lock);
	list_del_item(struct fuse_req,req);
	pthread_mutex_unlock(&req->se->lock);
	free(req);
}

int send_reply_ok(fuse_req_p req, const void *arg, size_t argsize)
{
	return send_iov_reply(req, 0, arg, argsize);
}

int send_reply_err(fuse_req_p req, int err)
{
	return send_iov_reply(req, -err, NULL, 0);
}

static unsigned long calc_timeout_sec(double t)
{
	if (t > (double) ULONG_MAX)
		return ULONG_MAX;
	else if (t < 0.0)
		return 0;
	else
		return (unsigned long) t;
}

static unsigned int calc_timeout_nsec(double t)
{
	double f = t - (double) calc_timeout_sec(t);
	if (f < 0.0)
		return 0;
	else if (f >= 0.999999999)
		return 999999999;
	else
		return (unsigned int) (f * 1.0e9);
}

static void convert_stat(struct fuse_attr *attr, const struct stat *stbuf)
{
	attr->ino	= stbuf->st_ino;
	attr->mode	= stbuf->st_mode;
	attr->nlink	= stbuf->st_nlink;
	attr->uid	= stbuf->st_uid;
	attr->gid	= stbuf->st_gid;
	attr->rdev	= stbuf->st_rdev;
	attr->size	= stbuf->st_size;
	attr->blksize	= stbuf->st_blksize;
	attr->blocks	= stbuf->st_blocks;
	attr->atime	= stbuf->st_atime;
	attr->mtime	= stbuf->st_mtime;
	attr->ctime	= stbuf->st_ctime;
	attr->atimensec=stbuf->st_atim.tv_nsec;
	attr->mtimensec=stbuf->st_mtim.tv_nsec;
	attr->ctimensec=stbuf->st_ctim.tv_nsec;
}

static void fill_entry(struct fuse_entry_out *arg,
		       const struct fuse_entry_param *e)
{
	arg->nodeid = e->ino;
	arg->generation = e->generation;
	arg->entry_valid = calc_timeout_sec(e->entry_timeout);
	arg->entry_valid_nsec = calc_timeout_nsec(e->entry_timeout);
	arg->attr_valid = calc_timeout_sec(e->attr_timeout);
	arg->attr_valid_nsec = calc_timeout_nsec(e->attr_timeout);
	convert_stat(&arg->attr,&e->attr);
}


int send_reply_entry(fuse_req_p req, const struct fuse_entry_param *e)
{
    struct fuse_entry_out arg;
	size_t size = req->se->conn.proto_minor < 9 ?
		FUSE_COMPAT_ENTRY_OUT_SIZE : sizeof(arg);

	/* before ABI 7.4 e->ino == 0 was invalid, only ENOENT meant
	   negative entry */
	if (!e->ino && req->se->conn.proto_minor < 4)
		return send_reply_err(req, ENOENT);

    memset(&arg, 0, sizeof(arg));
	fill_entry(&arg, e);
	return send_reply_ok(req, &arg, size);
}

static void fill_open(struct fuse_open_out *arg,
					  const struct fuse_file_info *f)
{
	arg->fh = f->fh;
	if (f->direct_io)
		arg->open_flags |= FOPEN_DIRECT_IO;
	if (f->keep_cache)
		arg->open_flags |= FOPEN_KEEP_CACHE;
	if (f->nonseekable)
		arg->open_flags |= FOPEN_NONSEEKABLE;
	if (f->cache_dir)
		arg->open_flags |= FOPEN_CACHE_DIR;
}

int send_reply_open(fuse_req_p req, const struct fuse_file_info *f)
{
	struct fuse_open_out arg;
	memset(&arg, 0, sizeof(arg));
	fill_open(&arg, f);
	return send_reply_ok(req, &arg, sizeof(arg));
}

int send_reply_create(fuse_req_p req, const struct fuse_entry_param *e,
		      const struct fuse_file_info *f)
{
	char buf[sizeof(struct fuse_entry_out) + sizeof(struct fuse_open_out)];
	size_t entrysize = req->se->conn.proto_minor < 9 ?
		FUSE_COMPAT_ENTRY_OUT_SIZE : sizeof(struct fuse_entry_out);
	struct fuse_entry_out *earg = (struct fuse_entry_out *) buf;
	struct fuse_open_out *oarg = (struct fuse_open_out *) (buf + entrysize);

	memset(buf, 0, sizeof(buf));
	fill_entry(earg, e);
	fill_open(oarg, f);
	return send_reply_ok(req, buf,
			     entrysize + sizeof(struct fuse_open_out));
}

static ssize_t fuse_buf_dst_fd(const struct fuse_buf *dst, size_t dst_off,
			      const struct fuse_buf *src, size_t src_off, size_t len)
{
	ssize_t res = 0;
	size_t copied = 0;

	while (len) {
		if (dst->flags & FUSE_BUF_FD_SEEK) {
			res = pwrite(dst->fd, (char *)src->mem + src_off, len,
				     dst->pos + dst_off);
		} else {
			res = write(dst->fd, (char *)src->mem + src_off, len);
		}
		if (res == -1) {
			if (!copied)
				return -errno;
			break;
		}
		if (res == 0)
			break;

		copied += res;
		if (!(dst->flags & FUSE_BUF_FD_RETRY))
			break;

		src_off += res;
		dst_off += res;
		len -= res;
	}

	return copied;
}

static ssize_t fuse_buf_src_fd(const struct fuse_buf *dst, size_t dst_off,
			     const struct fuse_buf *src, size_t src_off, size_t len)
{
	ssize_t res = 0;
	size_t copied = 0;

	while (len) {
		if (src->flags & FUSE_BUF_FD_SEEK) {
			res = pread(src->fd, (char *)dst->mem + dst_off, len,
				     src->pos + src_off);
		} else {
			res = read(src->fd, (char *)dst->mem + dst_off, len);
		}
		if (res == -1) {
			if (!copied)
				return -errno;
			break;
		}
		if (res == 0)
			break;

		copied += res;
		if (!(src->flags & FUSE_BUF_FD_RETRY))
			break;

		dst_off += res;
		src_off += res;
		len -= res;
	}

	return copied;
}

// 首先从 src 读取到 buf，再从 buf 读取到 dst
static ssize_t fuse_buf_fd_to_fd(const struct fuse_buf *dst, size_t dst_off,
				 const struct fuse_buf *src, size_t src_off, size_t len)
{
	char buf[4096];
	struct fuse_buf tmp = {
		.size = sizeof(buf),
		.flags = 0,
	};
	ssize_t res;
	size_t copied = 0;

	tmp.mem = buf;

	size_t this_len;
	size_t read_len;
	while (len) {
		this_len=tmp.size<len?tmp.size:len;
		res = fuse_buf_src_fd(&tmp, 0, src, src_off, this_len);
		if (res < 0) {
			if (!copied)
				return res;
			break;
		}
		if (res == 0)
			break;

		read_len = res;
		res = fuse_buf_dst_fd(dst, dst_off, &tmp, 0, read_len);
		if (res < 0) {
			if (!copied)
				return res;
			break;
		}
		if (res == 0)
			break;

		copied += res;

		if (res < this_len)
			break;

		dst_off += res;
		src_off += res;
		len -= res;
	}

	return copied;
}


static ssize_t fuse_buf_copy_one(const struct fuse_buf *dst, size_t dst_off,
				 const struct fuse_buf *src, size_t src_off, size_t len)
{
	int src_is_fd = src->flags & FUSE_BUF_IS_FD;
	int dst_is_fd = dst->flags & FUSE_BUF_IS_FD;
	if(!src_is_fd&&!dst_is_fd){
		char *dstmem = (char *)dst->mem + dst_off;
		char *srcmem = (char *)src->mem + src_off;

		if (dstmem != srcmem) {
			if (dstmem + len <= srcmem || srcmem + len <= dstmem)
				memcpy(dstmem, srcmem, len);
			else
				memmove(dstmem, srcmem, len);
		}
		return len;
	}else if(!src_is_fd){
		return fuse_buf_dst_fd(dst,dst_off,src,src_off,len);
	}else if(!dst_is_fd){
		return fuse_buf_src_fd(dst,dst_off,src,src_off,len);
	}else{
		return fuse_buf_fd_to_fd(dst,dst_off,src,src_off,len);
	}
}

int send_reply_read(fuse_req_p req,const struct fuse_buf *buf)
{
	struct fuse_buf tmp = {
		.size = buf->size,
		.flags = 0,
		.mem=NULL
	};
	tmp.mem=malloc(tmp.size);
	if(tmp.mem==NULL)
		send_reply_err(req,errno);
	int res=fuse_buf_copy_one(&tmp,0,buf,0,tmp.size);
	if (res<0)
		res=send_reply_err(req,errno);
	else
	{
		res=send_reply_ok(req,tmp.mem,res);
	}
	free(tmp.mem);
	return res;
}

int send_reply_write(fuse_req_p req, const struct fuse_buf *outbuf, const struct fuse_buf *inbuf)
{
	int res=fuse_buf_copy_one(outbuf,0,inbuf,0,outbuf->size);
	if (res<0)
		return send_reply_err(req,errno);
	else
	{
		struct fuse_write_out arg;
		memset(&arg, 0, sizeof(arg));
		arg.size = res;
		return send_reply_ok(req, &arg, sizeof(arg));
	}
}

int send_reply_attr(fuse_req_p req, const struct stat *stbuf,
		    double attr_timeout)
{
	struct fuse_attr_out arg;
	size_t size = req->se->conn.proto_minor < 9 ?
		FUSE_COMPAT_ATTR_OUT_SIZE : sizeof(arg);

	memset(&arg, 0, sizeof(arg));
	arg.attr_valid = calc_timeout_sec(attr_timeout);
	arg.attr_valid_nsec = calc_timeout_nsec(attr_timeout);
	convert_stat(&arg.attr,stbuf);

	return send_reply_ok(req, &arg, size);
}