#ifndef _FUSE_HELPER_
#define _FUSE_HELPER_
#include "cmakeConfig.h"
#include "fuse_option.h"
#include "fuse_session.h"
#include "fuse_operation.h"
#include "fuse_signal.h"
#include "fuse_mount.h"
#include "fuse_loop.h"
#include "fuse_error.h"
#include "fuse_crash.h"

#include <sys/mman.h>
// 正常模式下启动文件系统，启动成功的话，这个函数会依次调用如下函数：
// 1. `parse_cmd_opts`;
// 2. `fuse_session_new`;
// 3. `fuse_set_signal_handlers`;
// 4. `fuse_session_mount`;
// 5. `fuse_daemonize`;
// 6. `fuse_single_session_loop` or `fuse_multi_session_loop`
// @param args 待解析的参数
// @param ops 用户自定义的 fuse 文件系统操作
// @param userdata 用户自定义的 fuse 文件系统相关数据
// @param helper 用户自定义的打印帮助信息的函数
// @return 返回负数表示启动失败或者执行过程中因为错误退出；返回 0 表示正常退出；返回正数表示因为收到预期的信号而退出
int fuse_normal_mode(struct fuse_args* args, struct fuse_ops* ops, void* userdata, void (*helper)(void));

// 故障恢复模式下启动文件系统，启动成功的话，这个函数会依次调用如下函数：
// 1. `parse_cmd_opts`;
// 2. `fuse_session_new`;
// 3. `fuse_set_signal_ignore`;
// 4. `fuse_session_mount`;
// 5. `fuse_daemonize`;
// 6. `fuse_crash_recovery_start`
// 区别于 `fuse_normal_mode` 这个函数会额外创建一个进程用于故障恢复
// @param args 待解析的参数
// @param ops 用户自定义的 fuse 文件系统操作
// @param userdata 用户自定义的 fuse 文件系统相关数据
// @param helper 用户自定义的打印帮助信息的函数
// @param crhandlers 所有 crash recovery 需要的函数
// @return 返回负数表示启动失败或者执行过程中因为错误退出；返回 0 表示正常退出；返回正数表示因为收到预期的信号而退出
int fuse_crash_recovery_mode(struct fuse_args *args, struct fuse_ops *ops, void *userdata, void (*helper)(void),
                            struct fuse_crash_recovery_handlers crhandlers);

#endif