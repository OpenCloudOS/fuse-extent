#ifndef _FUSE_REPLY_H
#define _FUSE_REPLY_H

#include "fuse_req.h"
#include "fuse_kernel.h"
#include "fuse_session.h"

#include <assert.h>
#include <sys/uio.h>

// 缓冲区为某个文件描述符，这个字段设置之后 .mem 无效
#define FUSE_BUF_IS_FD 1
// 是否允许文件偏移
#define FUSE_BUF_FD_SEEK 2
// 重复直到 .size 字节全部被用完
#define FUSE_BUF_FD_RETRY 4
// 数据读写的缓冲区
struct fuse_buf {
	size_t size;    // 缓冲区大小
    void *mem;      // 指向缓冲区的指针

	int flags;		// 标志
    int fd;;		// 只有当 FUSE_BUF_IS_FD 设置时，这个字段才有用
	off_t pos;		// 只有当 FUSE_BUF_FD_SEEK 设置时，这个字段才有用
};

// 发送 iov 中的数据，如果 clonefd 为 -1 ，则将数据发往 se->fd；否则发往 se->fd
// @param se 请求所在的会话主体
// @param clonefd 在 clonefd 设置时，克隆的 /dev/fuse 文件描述符
// @param 数据
// @param 数据个数
// @return 发送成功返回 0，发送失败返回对应的错误号
int fuse_send_iov_msg(struct fuse_session *se, int clonefd,
							 struct iovec *iov, int count);

// 创建一个 iovec 结构体数组；第一项记录响应头部字段 fuse_out_header；第二项记录响应主体字段 arg（如果有的话）
// 发送响应内容，同时在发送往之后释放分配的请求
// @param req 请求体
// @param error 表示错误号（如果是因为错误而发送响应的话）
// @arg 响应主体
// @argsize 响应主体大小
// @return 发送成功返回 0，发送失败返回对应的错误号
int send_iov_reply(fuse_req_p req, int error,
						  const void *arg, size_t argsize);

void send_reply_none(fuse_req_p req);

int send_reply_ok(fuse_req_p req, const void *arg, size_t argsize);

int send_reply_err(fuse_req_p req, int err);

int send_reply_entry(fuse_req_p req, const struct fuse_entry_param *e);

int send_reply_open(fuse_req_p req, const struct fuse_file_info *f);

int send_reply_create(fuse_req_p req, const struct fuse_entry_param *e, const struct fuse_file_info *f);

int send_reply_read(fuse_req_p req, const struct fuse_buf *buf);

int send_reply_write(fuse_req_p req, const struct fuse_buf *outbuf, const struct fuse_buf *inbuf);

int send_reply_attr(fuse_req_p req, const struct stat *stbuf, double attr_timeout);

#endif