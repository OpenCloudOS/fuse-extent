#ifndef _FUSE_CRASH_H
#define _FUSE_CRASH_H
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

typedef int (*init_func) (void);
typedef void (*destroy_func) (void);
typedef void* (*notify_handler_routine) (void *);
typedef void (*crash_recovery_func) (void);

struct fuse_crash_recovery_handlers{
    init_func init;                     // 故障恢复初始化函数
    destroy_func destroy;               // 故障恢复结束函数
    notify_handler_routine nhandler;    // 故障恢复需要创建的例程函数
    crash_recovery_func crfunc;         // 故障恢复函数
};

// 通过 sendmsg 发送文件描述符到另外一个进程
// @param fd 套接字通道，用于发送
// @param sendbuf 发送数据缓冲区
// @param size 发送数据缓冲区大小
// @param sendfd 需要发送的文件描述符
// @return 成功返回发送的数据大小，失败返回 -1
int send_fd(int fd, void *sendbuf, size_t size, int sendfd);

// 通过 recvmsg 发送文件描述符到另外一个进程
// @param fd 套接字通道，用于接收
// @param recvbuf 接收数据缓冲区
// @param size 接收数据缓冲区大小
// @param recvfd 接收到的文件描述符
// @return 成功返回接收的数据大小，失败返回 -1
int recv_fd(int fd, void *recvbuf, size_t size, int* recvfd);

#endif