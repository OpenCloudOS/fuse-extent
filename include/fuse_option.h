#ifndef _FUSE_OPTION_H
#define _FUSE_OPTION_H

#include "fuse_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <sys/errno.h>
#include <sys/mount.h>

#define FUSE_ARGS_INIT(argc, argv) {argc,argv,0}

#define DEFAULT_THREAD_NUM 10
#define FUSE_CMD_OPTS_INIT {0, 0, 0, 0, 0, NULL, 0,DEFAULT_THREAD_NUM}

#define FUSE_MNT_OPTS_INIT {0, 0, 0, NULL, NULL, NULL}

#define DEFAULT_MAX_WRITE UINT_MAX
#define DEFAULT_MAX_READ 0
#define DEFAULT_MAX_READAHEAD 4
#define DEFAULT_MAX_BACKGROUND 4
#define DEFAULT_CONGESTION_THRESHOLD 3
#define DEFAULT_TIME_GRAN 1
#define FUSE_CONN_INFO_INIT {0,0,DEFAULT_MAX_WRITE,DEFAULT_MAX_READ,DEFAULT_MAX_READAHEAD,DEFAULT_MAX_BACKGROUND,DEFAULT_CONGESTION_THRESHOLD,DEFAULT_TIME_GRAN,0,0,{0}}

#define DEFINE_FUSE_OPT(s, t, p) {s, offsetof(t, p), 1}

#define FUSE_OPT_END {NULL,0,0}

// 指定需要解析的命令行命令的格式
struct fuse_opt
{
    const char *str;
    size_t offset;
    int value;
};

// 存储命令行中传入的参数
struct fuse_args
{
    int argc;
    char **argv;
    int allocated;  //是否是动态分配的参数，如果是，需要在使用完手动释放
};

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
    unsigned threads;     // 多线程情况下，处理请求线程
};

// 文件系统挂载相关配置
struct fuse_mnt_opts{
    int allow_other;      // 允许其他用户访问挂载的文件夹（内核或fusermount设置选项）
    int auto_unmount;     // 退出时自动解除挂载 (这个选项只在利用 fusermount 进行文件系统挂载时才有效)
    int flag;             // 挂载时的参数，默认为 MS_NOSUID | MS_NODEV
    char *flags;          // 用户传递的 flags 参数，最终会解析为 flag，也可以用来传递给 fusermount
    char *fsname;         // 指定文件系统的名字，如果没有设置则自动生成
	char *subtype;        // 文件系统类型 fuse.${subtype}，如果没有设置则最终的类型为 fuse
    //char *kernel_opts;  // 传递FUSE内核额外的参数（用户输入时需要根据内核指定的格式设置）
};

// 管理连接的信息，会被 `do_init()` 使用
// 有些字段是可读可写的，因此它能够根据文件系统的需求设置
// 只读表示用户不能通过启动守护进程时的参数设置
// 读写表示用户可以通过在启动守护进程时调整参数设置
struct fuse_conn_info {

	// 内核所使用的协议主版本号（只读）
	unsigned proto_major;

	// 内核所使用的协议次版本号（只读）
	unsigned proto_minor;

	// 最大写缓存（读写）
	unsigned max_write;

	// 最大能够读取请求的大小（读写）
	// 0 表示没有限制，但是实际上能最大读取请求的大小仍然会收到内核的限制
	// 这个字段似乎没有用到
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
	// 内核的时间粒度以纳秒为单位 (10^x)，而文件系统的时间粒度如果与内核不同，则需要通过这个字段通知内核（读写）
	unsigned time_gran;

	// fuse 内核所能支持能力的标记（只读）
	uint32_t capable;

	// libfuse 希望文件系统拥有能力的标记（只读）
	uint32_t want;

	// 保留字段
	unsigned reserved[22];
};

// 根据 opts 中的规则，解析 args 中的参数，结果存储在 data；
// @param args 所有命令行参数，解析过程中会从中移除已经解析的参数
// @param data 存储参数解析结果的字段
// @param opts 解析规则
// @return 0 on success, -1 on failure
int fuse_opts_parse(struct fuse_args *args, void *data, const struct fuse_opt opts[]);
// 如果 args 中的成员 argv 是动态分配的，则释放其中动态分配的内存
void free_fuse_args(struct fuse_args *args);

// 解析 fuse_cmd_opts 相关的参数；
// 解析命令行输入参数 args，并根据命令行参数设置 opts 选项；
// 成功解析的输入参数会从 args 中移除，未知的参数会保留在 args 当中；
// @param args 所有命令行参数，解析过程中会从中移除已经解析的参数
// @param opts 输出的参数解析结果
// @return 0 on success, -1 on failure
int parse_cmd_opts(struct fuse_args *args, struct fuse_cmd_opts *opts);
// 释放 fuse_cmd_opts 中动态分配的参数 (mountpoint)
void free_cmd_opts(struct fuse_cmd_opts *opts);

// 解析 fuse_mnt_opts 相关的参数；
// 解析命令行输入参数 args，并根据命令行参数设置 opts 选项；
// 成功解析的输入参数会从 args 中移除，未知的参数会保留在 args 当中；
// @param args 所有命令行参数，解析过程中会从中移除已经解析的参数
// @param opts 输出的参数解析结果
// @return 0 on success, -1 on failure
int parse_mnt_opts(struct fuse_args *args, struct fuse_mnt_opts *opts);
// 释放 fuse_mnt_opts 中动态分配的参数 (flags, fname, subtype)
void free_mnt_opts(struct fuse_mnt_opts *opts);

// 解析 fuse_conn_info 相关的参数；
// 解析命令行输入参数 args，并根据命令行参数设置 opts 选项；
// 成功解析的输入参数会从 args 中移除，未知的参数会保留在 args 当中；
// @param args 所有命令行参数，解析过程中会从中移除已经解析的参数
// @param opts 输出的参数解析结果
// @return 0 on success, -1 on failure
int parse_conn_info(struct fuse_args *args, struct fuse_conn_info *info);

void fuse_help();

// 显示 fuse_cmd_opts 参数相关的帮助
void fuse_cmd_help();

// 显示 fuse_mnt_opts 参数相关的帮助
void fuse_mnt_help();

// 显示 fuse_conn_info 参数相关的帮助
void fuse_conn_help();

#endif