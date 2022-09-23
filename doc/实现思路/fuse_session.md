# 总览
`fuse_session.c` 文件主要实现了一个新的会话建立的过程，但是它仅仅只是创建一个 fuse_session 结构体，并不做其他任何挂载以及监听的工作。

## 数据结构
```c
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
```
其中各个参数字段的说明如下：
1. mountpoint 是记录挂载点绝对地址的字符串，这个字段会在挂载成功后指向 fuse_cmd_opts 中的挂载点；
2. mo 记录了挂载相关的参数设置；
3. conn 记录了在 `do_init()` 初始化时需要与 fuse 内核协商的参数；
4. fd 记录打开的 /dev/fuse 对应的文件描述符，这个字段被初始为 -1，并在成功挂载后设置为对应的值；
5. clonefds 记录了在多线程 clonefd 模式下，所有克隆（打开）的文件描述符，只有在文件系统解除挂载或者 destroy 时才关闭这些文件描述符。
6. debug 记录是否处于调试模式，根据 fuse_cmd_opts.debug 进行设置；
7. inited 会在 `do_init()` 完成后被设置为 1；
8. destroyed 会在 `do_destroy()` 或者 `fuse_session_destroy` 完成后被设置为 1；
9. exited 记录 `fuse_session_loop` 是否退出，会在某个循环线程因为错误或者信号退出时被设置为 1，其他线程随后也会退出；
10. ops 记录用户自定义的文件系统操作集合；
11. owner 记录了创建会话以及随后挂载文件系统的用户 id；
12. userdata 用户自定义的数据；
13. lock 管理 req_list 以及 int_list 的锁，主要在多线程过程处理请求时有用；
14. req_list 普通请求队列（单线程情况下，这个队列最多只有一个请求，接收一个处理一个）
15. int_list 打断请求队列（同样，单线程情况下，这个队列最多只有一个请求）
16. bufize 用来接收请求的缓冲区大小
17. error 如果会话通过信号中止，那么这个字段被设置为信号的值

## 接口函数
1. `struct fuse_session *fuse_session_new(struct fuse_args *args, const struct fuse_ops *ops, int debug, void* userdata)` 创建一个新的会话，创建成功则返回对应的会话，失败则返回 NULL，这个函数会设置会话所需要的参数以及调用 `parse_mnt_opts` 和 `parse_conn_info` 解析用户传入的相关参数。注意：这个函数只是创建一个 fuse_session 结构体，但是它并不进行实质的挂载以及初始化操作，这些操作都需要在后续完成。
2. `void fuse_session_reset(struct fuse_session *se)` 重置当前会话，它的作用就是设置 exited 以及 error 为0。这个函数会在 `fuse_session_loop` 退出后调用，可以用于故障后重启。
3. `void fuse_session_destroy(struct fuse_session *se)` 这个函数一般在文件系统解除挂载后进行最后的清理工作。i. 如果有 ops.destroy 函数，则调用这个函数；ii. 清理分配的锁 lock；iii. 如果文件描述符未关闭（一般会在 `fuse_session_umount()` 中关闭），就关闭文件描述符；iv. 释放 mo 中动态分配的内存；v. 释放 se。注意，这个函数不需要请求 req_list 以及 int_list 中动态分配的请求，因为请求一旦产生，它一定会在退出 `fuse_session_loop` 之前被释放。