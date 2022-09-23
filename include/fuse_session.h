#ifndef _FUSE_SESSION_H
#define _FUSE_SESSION_H
#include "fuse_req.h"
#include "fuse_option.h"	
#include "fuse_operation.h"

#include <unistd.h>
#include <pthread.h>

struct fuse_session{
	const char* mountpoint;		// 挂载点绝对地址，在挂载成功后被初始化
	struct fuse_mnt_opts mo;	// 有关挂载相关的参数设置
	struct fuse_conn_info conn; // 会话相关的信息，在 `do_init()` 中进行协商
	int fd;						// 记录打开的 /dev/fuse 对应的文件描述符
	int *clonefds;				// 如果在多线程模式下开启了 clonefd，这个字段是所有克隆文件描述符的数组，用于解除挂载时关闭这些文件描述符
	int debug;					// 是否处于调试模式，根据 fuse_cmd_opts.debug 来设置
	int inited;					// 是否已经在 `do_init()` 中初始化
	int destroyed;				// 是否已经在 `do_destroy()` 中销毁
	volatile int exited;		// 是否退出
	struct fuse_ops ops;		// FUSE 文件系统下自定义的操作（根据 VFS 操作接口格式）
	uid_t owner;				// 创建会话的用户 ID
	void *userdata;				// 用户自定义数据
	pthread_mutex_t lock;		// 应该是访问 req_list 以及 int_list 的锁
	struct fuse_req req_list;	// 正常用户请求队列
	struct fuse_req int_list;	// interrupt 请求队列
	size_t bufsize;				// 接收从内核传来请求的缓冲区大小
	int error;					// 进程如果因为信号而被中断，则这个字段会被设置
};

// 根据 args 以及 op 参数创建一个会话 session；
// 这个 session 随后会给传递给 fuse_session_mount() 以及 fuse_session_loop()；
// 通过 man fuse 查看 fuse 支持的配置选项；
// argv[0] 总是应该包含当前执行的程序绝对路径
// @param args 相关参数
// @param op 文件系统操作函数
// @param debug 是否开启调试模式
// @param userdata 用户数据
// @return created fuse session on success, NULL on failure
struct fuse_session *fuse_session_new(struct fuse_args *args, const struct fuse_ops *ops, int debug, void* userdata);

// 重置会话，设置 se->exited=0, se->error=0
// @param se session 对象
void fuse_session_reset(struct fuse_session *se);

// 这个函数一般在文件系统解除挂载后进行最后的清理工作:
// 1. 如果有 ops.destroy 函数，则调用这个函数；
// 2. 清理分配的锁 lock；
// 3. 如果有打开的 clonefds，则关闭对应的文件描述符
// 4. 如果文件描述符未关闭（一般会在 `fuse_session_umount()` 中关闭），就关闭文件描述符；
// 5. 释放 mo 中动态分配的内存；
// 6. 释放 se；
// @param se session 对象
void fuse_session_destroy(struct fuse_session *se);

#endif