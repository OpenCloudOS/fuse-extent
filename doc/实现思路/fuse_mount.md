# 总览
`fuse_mount.c` 主要实现了文件系统的挂载与解除挂载操作
## 接口函数
1. `int fuse_session_mount(struct fuse_session *se, const char *mountpoint)` 这个函数根据 se->mo 信息在 mountpoint 挂载文件系统，挂载成功返回 0，失败返回 -1；
2. `void fuse_session_unmount(struct fuse_session *se)` 这个函数解除在 se->mountpoint 处挂载的文件系统
## 实现方式
1. `fuse_session_mount()` 进一步调用 `fuse_kern_mount()` 进行挂载，如果挂载成功，那么设置 se->fd 为打开的 /dev/fuse 文件描述符，以及 se->mountpoint 为挂载点绝对路径，挂载成功的话 se->mountpoint 为一个指向 fuse_cmd_opts.mountpoint 字符串的指针，它不需要调用 free() 主动释放。`fuse_kern_mount()` 接着会首先调用 `fuse_mount_sys()` 进行挂载（这里将会通过系统调用 `mount()` 进行挂载），如果挂载失败且返回值为 -2，那么会再调用 `fuse_mount_fusermount()` 进行挂载（这里则是通过运行 fusermount 程序进行挂载）。挂载时需要用到 se->mo 中给定的挂载参数。如果挂载参数设置了 auto_unmount，则只能通过 `fuse_mount_fusermount()`进行挂载，因为只有这种方式才支持 auto_unmount。
2. `fuse_session_unmount()` 进一步调用 `fuse_kern_unmount()` 进行解挂，`fuse_kern_unmount()` 会关闭打开的文件描述符，接着，通过 umount2() 系统调用或者 fusermount 进行解除挂载，最后 `fuse_session_unmount()` 会设置 se->fd=-1, se->mountpoint=NULL。
## 其他
在 `fuse_mount_fusermount()` 中，主进程 fork 一个子进程，两个进程之间通过 socket pair（类似于 pipe）进行通信。子进程中调用 fusermount 进行挂载，fusermount 会一直持续到父进程的读端口关闭。在 auto_unmount 设置时会在子进程退出后自动解除挂载。因此，在 auto_unmount 设置时，这个函数不会主动关闭父进程读端口，因此子进程会一直存在，可以通过 `ps -ef|grep fusermount` 观察。只有在父进程结束时，程序的清理工作会自动关闭读端口，然后文件系统自动解除挂载。在 auto_unmount 未设置时，在 `fuse_mount_fusermount()` 在挂载成功后，会主动关闭父进程读端口，然后等待子进程退出。