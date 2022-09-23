# 总览
`fuse_option.c` 文件主要实现了对用户传入参数的解析过程。用户传入参数的格式在 `fuse_cmd_help(), fuse_mnt_help(), fuse_conn_help()` 中定义，如下：
```
fuse cmdline options: 
    [-h, --help]                 print help
    [-v, --version]              print version
    [-d, --debug]                enable debug output
    [-f, --foreground]           foreground operation
    [-m, --multithread]          enable multi-thread operation
    [-c, --clonefd]              clone /dev/fuse for every thread under multithread mode
    [-t, --threads=%u]           number of worker threads in multi-thread mode (default=10)
fuse mount options: 
    [--allow_other]              allow access by all users
    [--auto_unmount]             auto unmount the filesystem when the daemonize end (this option only works in fusermount)
    [--fsname=%s]                name of the mounted FUSE filesystem
    [--subtype=%s]               type=fuse.${subtype}
    [--flags=<flag[,flag]...>]   flag for mount the filesystem (default=nosuid,nodev)
    Available flags: rw, ro, suid, nosuid, dev, nodev, exec, noexec, async, sync, atime, noatime
fuse connection init options: 
    [--max_write=%u]             maximum number of written data (default=UINT_MAX)
    [--max_read=%u]              maximum number of read data (default=0)
    [--max_readahead=%u]         maximum readahead (default=4)
    [--max_background=%u]        maximum readahead (default=4)
    [--congestion_threshold=%u]  congestion threshold (default=3)
    [--time_gran=%u]             time granularity (ns) of the filesystem (default=1)
```
其中，%u 表示参数会按照 unsigned 的格式进行解析，%s 表示参数会找找字符串的形式进行解析。
# 数据结构
定义了 3 个保存用户传入参数的数据结构，分别为 `struct fuse_cmd_opts, struct fuse_mnt_opts, struct fuse_conn_info`，其中：
- struct fuse_cmd_opts 用来保存程序运行时的相关配置选项，如是否打印帮助信息、版本信息，是否处于调试模式下运行等，此外调试模式下进程默认在前台运行不会挂载为守护进程；
- struct fuse_mnt_opts 保存了挂载文件系统所需要的参数，如是否允许其他人访问挂载目录等；
- struct fuse_conn_info 这个结构体用来保存连接建立时需要协商的参数，它会在 `do_init` 过程中被协商，用户可以通过命令行参数对其中的一些读写字段进行设置；
```c
// 命令行相关的配置选项
struct fuse_cmd_opts
{
    int help;             // 显示帮助信息
    int version;          // 显示版本信息
    int debug;            // 调试模式
    int foreground;       // 进程在前台运行或是设置为守护进程（在调试模式下，默认在前台运行）
    int multithread;      // 是否多线程处理请求
    char* mountpoint;     // 挂载点
	int clonefd;		  // 在多线程模式下，是否为每个线程拷贝 /dev/fuse，这个选项可以加快多线程模式下的处理速度
    unsigned threads;     // 多线程情况下，处理请求线程数
};

struct fuse_mnt_opts{
    int allow_other;     // 允许其他用户访问挂载的文件夹（内核或fusermount设置选项）
    int auto_unmount;    // 退出时自动解除挂载 (这个选项只在利用 fusermount 进行文件系统挂载时才有效)
    int flag;            // 调用 mount() 时的参数，默认为 MS_NOSUID | MS_NODEV
    char *flags;         // 用户传递的 flags 参数，最终会解析为 flag，也可以用来传递给 fusermount
    char *fsname;        // 指定文件系统的名字，如果没有设置则自动生成
	char *subtype;       // 文件系统类型 fuse.${subtype}，如果没有设置则最终的类型为 fuse
    //char *kernel_opts;  // 传递FUSE内核额外的参数（用户输入时需要根据内核指定的格式设置）
};

struct fuse_conn_info {

	// 内核所使用的协议主版本号（只读）
	unsigned proto_major;

	// 内核所使用的协议次版本号（只读）
	unsigned proto_minor;

	// 最大写缓存（读写）
	unsigned max_write;

	// 最大能够读取请求的大小（读写）
	// 0 表示没有限制，但是实际上能最大读取请求的大小仍然会收到内核的限制
	// 这个字段似乎没有用到，应该是在mnt_opts 中有用
	unsigned max_read;

	// （读写）
	unsigned max_readahead;

	// 限制 background 队列中的请求被移动到 pending 队列中的数量
	// 当 pending 队列中的请求数量少于 max_background 时，
	// background 队列中的请求将会被移入 pending 队列（读写）
	unsigned max_background;

	// 拥塞阈值，当 pending 队列中的请求达到这个阈值时，
	// 内核标记这个文件系统是处于拥塞装填，并相应地调整它的算法
	// 如，让原本处于忙等待地用户进程进入睡眠（读写）
	unsigned congestion_threshold;

	// 文件系统（守护进程）的时间粒度
	// 当 FUSE_CAP_WRITEBACK_CACHE 被设置时，写请求到达时内核将会负责更新 mtime 和 ctime
	// 内核的时间粒度以纳秒为当为，而文件系统的时间粒度如果与内核不同，则需要通过这个字段通知内核（读写）
	unsigned time_gran;

	// fuse 内核所能支持能力的标记（只读）
	uint32_t capable;

	// libfuse 希望文件系统拥有能力的标记（只读）
	uint32_t want;

	// 保留字段
	unsigned reserved[22];
};
``` 

# 接口函数

## 解析函数
1. `int parse_cmd_opts(struct fuse_args *args, struct fuse_cmd_opts *opts)` 这个函数将会解析命令行参数（整体配置选项），并将成功解析的结果从 args 中移除，并存在 struct fuse_cmd_opts *opts，保留未解析的结果。
2. `int parse_mnt_opts(struct fuse_args *args, struct fuse_mnt_opts *opts)` 这个函数将会解析命令行参数（文件系统挂载相关的配置选项），并将成功解析的结果从 args 中移除，并存在 struct fuse_mnt_opts *opts，保留未解析的结果。 一般首先调用 fuse_cmd_opts 对参数进行解析，解析后再调用 fuse_mnt_opts 对参数进行解析，而 fuse_mnt_opts 一般是在 `fuse_session_new` 函数中被调用。
3. `int parse_conn_info(struct fuse_args *args, struct fuse_conn_info *info)` 这个函数将会解析命令行参数（连接建立时的相关参数），并将成功解析的结果从 args 中移除，并存在 struct fuse_conn_info *info，保留未解析的结果。这个函数和 fuse_mnt_opts 的解析函数一样，会在 `fuse_session_new` 函数中被调用。
4. `int fuse_opts_parse(struct fuse_args *args, void *data, const struct fuse_opt opts[])`，上面的函数都会调用这个函数来帮助它们完成参数的解析，这个函数会借助 opts 中的规则来完成对 args 中参数的解析。

### 原理
我们定义了如下结构体来帮助参数解析：
```c
struct fuse_opt
{
    const char *str;
    size_t offset;
    int value;
};
```
对于 fuse_cmd_opts 结构体，我们再定义如下规则：
```c
#define DEFINE_FUSE_OPT(s, t, p) {s, offsetof(t, p), 1}
#define FUSE_OPT_END {NULL,0,0}
static const struct fuse_opt fuse_cmd_helper_opts[] = {
    DEFINE_FUSE_OPT("-h", struct fuse_cmd_opts, help),
    DEFINE_FUSE_OPT("--help", struct fuse_cmd_opts, help),
    DEFINE_FUSE_OPT("-v", struct fuse_cmd_opts, version),
    DEFINE_FUSE_OPT("--version", struct fuse_cmd_opts, version),
    DEFINE_FUSE_OPT("-d", struct fuse_cmd_opts, debug),
    DEFINE_FUSE_OPT("debug", struct fuse_cmd_opts, debug),
    DEFINE_FUSE_OPT("-f", struct fuse_cmd_opts, foreground),
    DEFINE_FUSE_OPT("--foreground", struct fuse_cmd_opts, foreground),
    DEFINE_FUSE_OPT("-m", struct fuse_cmd_opts, multithread),
    DEFINE_FUSE_OPT("--multithread", struct fuse_cmd_opts, multithread),
    DEFINE_FUSE_OPT("-c", struct fuse_cmd_opts, clonefd),
    DEFINE_FUSE_OPT("--clonefd", struct fuse_cmd_opts, clonefd),
    DEFINE_FUSE_OPT("-t=%u", struct fuse_cmd_opts, threads),
    DEFINE_FUSE_OPT("--threads=%u", struct fuse_cmd_opts, threads),
    FUSE_OPT_END
}
```
如第一个选项，`DEFINE_FUSE_OPT("-h", struct fuse_cmd_opts, help)`，它表示匹配到字符串 "-h" 时，将会设置 fuse_cmd_opts 中的 help 字段为 1。这是因为在 fuse_opt 结构体当中，第一个 str 字段记录了匹配的字符串 "-h"，第二个 offset 字段记录了 help 在 fuse_cmd_opts 中的偏移量，value 为将要设置的值。 我们在 fuse_opts_parse 根据解析规则 opts 对 data 进行设置，虽然我们不知道 data 的具体类型，但是我们知道当匹配到 "-h" 时，我们想要设置字段相对于 data 的便宜，我们就可以直接根据这个偏移量对 data 进行设置。

## 内存释放
1. `void free_fuse_args(struct fuse_args *args)` 这个函数将会释放 args 中动态分配的内存，fuse_args 这个结构体既可以保存一开始由用户通过命令行传入的参数，也可以保存由解析参数过程中动态生成的参数，而这些动态生成的参数需要在使用结束时释放。
2. `void free_cmd_opts(struct fuse_cmd_opts *opts)` 这个函数释放 fuse_cmd_opts 结构体中动态分配的内存，它的 mountpoint 字段是在参数解析过程中动态分配的。
3. `void free_mnt_opts(struct fuse_mnt_opts *opts)` 这个函数释放 fuse_mnt_opts 结构体中动态分配的内存，它的 flags, fsname, subtype 字段是在参数解析过程中动态分配的。
4. fuse_conn_info 中没有动态分配的内存，不需要主动释放
## 帮助
1. `void fuse_cmd_help()` 显示 fuse_cmd_opts 提示信息。
2. `void fuse_mnt_help()` 显示 fuse_mnt_opts 提示信息。
3. `void fuse_conn_help()` 显示 fuse_conn_opts 提示信息。
