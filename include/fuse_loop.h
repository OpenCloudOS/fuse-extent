#ifndef _FUSE_LOOP_H
#define _FUSE_LOOP_H

#include "cmakeConfig.h"
#include "fuse_req.h"
#include "fuse_kernel.h"
#include "fuse_option.h"
#include "fuse_session.h"
#include "fuse_reply.h"
#include "fuse_error.h"

#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/uio.h>

// 根据参数 foreground 确定是否创建守护进程
// @param foreground 如果为 true，则保持在前端继续运行，否则在后台创建守护进程并运行
// @return 0 on success, -1 on failure
int fuse_daemonize(int foreground);

// 单线程循环中从 /dev/fuse 接收请求，并处理请求
// @param se 代表当前会话，管理正在交互的 /dev/fuse 文件描述符
// @return 如果返回 0，表示因为文件系统解除挂载（unmount or /sys/fs/fuse/connections/NNN/abort）而退出；
// 如果返回一个正值，表示因为一个注册的信号 signal 被触发而退出；
// 如果返回一个负值，则表示因为运行过程中发生错误而退出，对应错误号
int fuse_single_session_loop(struct fuse_session *se);

// 创建多个线程，每个线程循环中从 /dev/fuse 接收请求，并处理请求
// @param se 代表当前会话，管理正在交互的 /dev/fuse 文件描述符
// @param clonefd 代表是否为每一个线程克隆一个 fuse_dev，在拥有大量请求的情况下，有助于加快请求响应速度
// @param threads 代表创建的线程数量
// @return 如果返回 0，表示因为文件系统解除挂载（unmount or /sys/fs/fuse/connections/NNN/abort）而退出；
// 如果返回一个正值，表示因为一个注册的信号 signal 被触发而退出；
// 如果返回一个负值，则表示因为运行过程中发生错误而退出，对应错误号
int fuse_multi_session_loop(struct fuse_session *se, int clonefd, unsigned threads);

// 故障恢复需要的函数
void fuse_session_set_ptr(void * ptr);
void fuse_session_recovery(struct fuse_session *se);

#endif