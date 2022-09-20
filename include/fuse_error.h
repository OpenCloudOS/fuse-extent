#ifndef _FUSE_ERROR_H
#define _FUSE_ERROR_H

// 错误号：多线程循环中没有一个线程创建成功
#define ENOTHREAD 0x1000
// 错误号：无法在多线程循环中启动故障修复
#define ERECOVERY 0x1001
// 错误号：在 fuse_helper 中使用，如果在文件系统启动过程中出错，而不是在 fuse_session_loop 过程中出错；
// 那么 fuse_normal_mode 和 fuse_crash_recovery_mode 最终会返回 -EBUILD, 而不是在 errno.h 中定义的错误号
// 如果文件系统在启动过程中就出错，那么不会进行故障恢复
#define EBUILD  0x1002

#endif