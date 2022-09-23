#ifndef _FUSE_REQ_H
#define _FUSE_REQ_H

#include "cmakeConfig.h"

#include<stdint.h>
#include<stdatomic.h>
#include<sys/types.h>

// 计算缓冲区默认大小
#define FUSE_MAX_MAX_PAGES 256
#define FUSE_DEFAULT_MAX_PAGES_PER_REQ 32
#define FUSE_BUFFER_HEADER_SIZE 0x1000

struct fuse_ctx
{
    /** User ID of the calling process */
    uid_t uid;

    /** Group ID of the calling process */
    gid_t gid;

    /** Thread ID of the calling process */
    pid_t pid;

    /** Umask of the calling process */
	mode_t umask;
};

struct fuse_req
{
    struct fuse_session *se;

	// 如果多线程模式下设置了 clonefd 选项，那么这个字段被设置为拷贝的文件描述符，
	// 否则这个字段为 -1
    int fd;

	// 一个请求要么被处理，那么被另外一个 int 请求打断，
	// 如果一个请求正在被处理，那么收到 int 不会处理，
	// 如果一个请求正在被打断，那么它不会被处理，
	// 一个请求只能处于正常处理或者打断处理中的一个
    volatile atomic_flag used;               

	// 请求 ID
    uint64_t unique;        

	// 收到一个新的请求，如果 int_list 中有对应的打断请求，
	// 那么新接收到的请求的这个字段将会被标记为 1，
	// 随后这个新接收到的请求不会被处理，而是直接释放
    int interrupted;

	// 对于一个 int 请求，这个字段设置为期望打断的请求号
    int interrupted_id;

	// 请求发起者的用户信息，进程信息等
    struct fuse_ctx ctx; 

    struct fuse_req* prev;
    struct fuse_req* next;
};
typedef struct fuse_req* fuse_req_p;
typedef uint64_t fuse_inode;

// struct fuse_notify_req
// {
//     struct fuse_notify_req *prev;
//     struct fuse_notify_req *next;
// };

#define FUSE_LIST_INIT(item) \
    {                       \
        item.prev = &item;    \
        item.next = &item;    \
    }

#define list_init_item(item) \
    {                      \
        item->next = item;   \
        item->prev = item;   \
    }

#define list_add_item(item, list) \
    {                           \
        item->next = &list;      \
        item->prev = list.prev;  \
        list.prev->next = item;  \
        list.prev = item;        \
    }

#define list_del_item(type,req)                 \
    {                                      \
        type *prev = req->prev; \
        type *next = req->next; \
        prev->next = next;                 \
        next->prev = prev;                 \
    }
#endif
