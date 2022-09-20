#ifndef _FUSE_MOUNT_H
#define _FUSE_MOUNT_H
#include "fuse_session.h"

#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <poll.h>

#define FUSERMOUNT_PROG "fusermount3"
#define FUSE_COMMFD_ENV "_FUSE_COMMFD"

// 根据 se 信息在 mountpoint 挂载一个 FUSE 文件系统
// @param mountpoint 挂载点绝对路径
// @param se session 对象
// @return 0 on success, -1 on failure.
int fuse_session_mount(struct fuse_session *se, const char *mountpoint);


// 解挂一个文件系统，并关闭打开的文件描述符，释放挂载点动态分配的内存；
// 这个函数将会通过 umount2() 或者 fusermount 进行解挂；
// 用户也可以在命令行中输入 umount 或者 fusermount 进行解挂；
// 在循环处理请求 `fuse_session_loop` 过程中解除挂载，将会导致循环退出，错误号为 ENODEV；
// 这个函数一般在循环处理请求在某种原因退出后调用；
// 如果循环处理请求 `fuse_session_loop` 退出后，文件系统仍然处于挂载状态；
// 那么任何对文件系统的操作都会阻塞（当文件系统进程仍在运行时），或者返回一个 ESHUTDOWN 错误（当文件系统进程中止时）；
// 第二种情况由两种可能的产生方式：
// 1. 文件系统守护进程意外退出，没有完成解除挂载的操作；
// 2. 通过往 /sys/fs/fuse/connections/NNN/abort 写数据，循环退出后调用这个函数，但是这个函数对于这种情况不会主动解除挂载；
// @param se session 对象
void fuse_session_unmount(struct fuse_session *se);

#endif