# 总览
`passthrough.c` 是一个具体的用户态文件系统实现的例子，使用到了之前所定义的接口。这个文件系统的功能是把挂载目录重定向到整个文件系统中某个已经存在的目录下，在这个挂载目录下的操作相当于在对应目录下的操作。
## 数据结构
```
struct lo_inode
{
	struct lo_inode *next; /* protected by lo->mutex */
	struct lo_inode *prev; /* protected by lo->mutex */
	int fd;
	ino_t ino;
	dev_t dev;
	uint64_t refcount; /* protected by lo->mutex */
};

struct lo_dirp
{
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

struct lo_data
{
	pthread_mutex_t mutex; // 循环遍历 lo_inode 的锁
	char *source;	       // 该文件系统被重定向的目标路径
	double timeout;
	struct lo_inode root; // 通过上面的 mutex 来控制访问
}; 
```

1. lo_inode 保存当前文件系统所有引用的文件，相当于 fuse 文件系统下的 inode 结构体，而其中 ino 字段则保存对应文件在其真实所在文件系统的 inode 号。 所有被引用的 lo_inode 结构体通过 next, prev 指针形成一个链表。 fd 保存在相应文件系统下打开这个文件的描述符，ino 是对应文件系统的 inode 号，dev 是对应文件系统的设备号，refcount 则是其在当前文件系统中的引用计数。refcount 每当某个文件被 lo_lookup 找到时，这个字段都会加 1，而被 lo_forget 时，这个字段会减去相应的数值。当引用计数为 0 时，这个 lo_inode 结构体将被释放。另外，每个文件在其所在的文件系统都有相应的 inode 号，而在 fuse 文件系统下，它的 inode 号则是 lo_inode 结构体在内存中的地址。通过 `typedef uint64_t fuse_inode` 来表示一个 lo_inode 结构体对应的 inode 号，另外用一个特殊的 inode 号 1 来表示根文件。
2. lo_data 保存这个 fuse 文件系统的整体信息，mutex 是用来循环遍历 lo_inode 的锁；source 表示该文件系统被重定向的目标路径；timeout 被简单地设置为0（当然还有更多设置，这里为了简化），表示相应的文件不会被缓存到内存，而是每次都是从磁盘直接读取文件并直接写回磁盘，root 保存该 fuse 文件系统根目录的 lo_inode。
3. 另外，还有一个结构体 `struct fuse_file_info`，其中一个字段 fh。它会在每次 `create(), open(), opendir()` 被设置为对应文件在其真实所在文件系统被真正打开之后的文件描述符，这个结构体会在这些请求的响应过程中被使用到。后续，用户可以通过 fh 这个字段，来对被重定向到目录的文件进行操作。

## 文件系统实现
### lo_destroy
`static void lo_destroy(void *userdata)`

这个函数会释放所有已经分配的 lo_inode 结构体的内存。对于 fuseblk，它会在 `do_destroy()` 中被调用；对于 fuse，由于不会产生 destroy 请求，因此只能在 `fuse_session_destroy()` 中调用。
### lo_lookup
`static int do_lookup(fuse_req_p req, fuse_inode parent, const char *name, struct fuse_entry_param *e)`

查找 parent 目录下，名为 name 的节点。如果找到，则在内存中创建 lo_inode 结构体，并加入链表（如果这个结构体在链表中已经存在的话，那么就对它的引用计数加 1）。这个函数需要设置 lo_data 结构体中的 fd 字段，表示在对应真实文件系统中打开文件的文件描述符，但是在 do_lookup 的过程中我们并不打开这个文件，通过 open 时设置 O_PATH 选项，仅仅获得这个文件对应的描述符，但不真正打开这个文件。另外，还需要设置 O_NOFOLLOW 选项。

### lo_forget
`static void lo_forget(fuse_req_p req, fuse_inode ino, uint64_t nlookup)`

这个函数将 fuse_inode 号为 ino 的 lo_inode 结构体的引用计数减去 nlookup，如果减完之后引用计数为0，那么释放这个结构体。

### lo_rename
`static void lo_rename(fuse_req_p req, fuse_inode parent, const char *name, fuse_inode newparent, const char *newname)`

实现移动及重命名功能

### 文件操作
#### lo_open
`static void lo_open(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)`

该函数通过首先获取 ino 号对应的 lo_inode 结构体，再根据其中的 fd 字段，打开 /proc/self/fd/ 目录下对应文件的文件，这个文件是指向实际要打文件的软连接。另外 open 中要设置 ~NO_NOFOLLOW. 最后需要设置 fi->fh 为实际打开的文件描述符。

```c
sprintf(buf, "/proc/self/fd/%i", lo_fd(req, ino));
fd = open(buf, fi->flags & ~O_NOFOLLOW);
```

### lo_create
`static void lo_create(fuse_req_p req, fuse_inode parent, const char *name, mode_t mode, struct fuse_file_info *fi)`

同样需要设置 fi->fh 为实际打开的文件描述符。

### lo_read
`static void lo_read(fuse_req_p req, fuse_inode ino, size_t size, off_t offset, struct fuse_file_info *fi)`

读取文件，结果写到 fi->fd 当中。

### lo_write
`static void lo_write(fuse_req_p req, fuse_inode ino, char *buf, size_t size, off_t off, struct fuse_file_info *fi)`

其中 buf 为输入缓冲区，结果写到 fi->fd 对应的文件。

### lo_unlink
`static void lo_unlink(fuse_req_p req, fuse_inode parent, const char *name)`

实现删除文件的功能。

### lo_release
`static void lo_release(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)`

关闭打开的文件描述符 fi->fd.

### lo_flush
`static void lo_flush(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)`

处于内存中的文件内容刷入磁盘

## 目录操作
目录也是文件，fuse_inode 记同样录这个目录对应文件 lo_inode 对象的内存地址。但是， fuse_file_info 中的 fh 字段不是打开目录文件的文件描述符，而是目录结构体 lo_dirp 分配的内存地址。

### lo_opendir
`static void lo_opendir(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)`

打开 ino 号对应的文件目录，并设置 fi->fh 为目录结构体 lo_dirp 分配的内存地址。

### lo_mkdir
`static void lo_mkdir(fuse_req_p req, fuse_inode parent, const char *name, mode_t mode)`

创建目录，但没有打开这个目录。这里和文件操作的 create 不同，文件操作的的 create 是调用 open 系统调用，不存在对应的文件就创建。

### lo_rmdir
`static void lo_rmdir(fuse_req_p req, fuse_inode parent, const char *name)`

实现移除目录的功能

### lo_readdir
`static void lo_readdir(fuse_req_p req, fuse_inode ino, size_t size, off_t offset, struct fuse_file_info *fi)`

首先根据 fi->fh 获得已经打开目录的 lo_dirp 结构体，然后从 offset 位置开始调用 readdir 系统调用读取目录中的每一个目录项（每一个目录项为 fuse_dirent 对象），结果保存在缓冲 buf 中，buf 的大小为 size。

### lo_releasedir
`static void lo_releasedir(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)`

通过 closedir 关闭 fi->fh 对应的 lo_dirp 结构体中的 DIR 结构体。

## lo_getattr
`static void lo_getattr(fuse_req_p req, fuse_inode ino, struct fuse_file_info *fi)`

通过 fstatat 系统调用获取文件属性，并设置 AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW flag.

## lo_setattr
`static void lo_setattr(fuse_req_p req, fuse_inode ino, struct stat *attr, int valid, struct fuse_file_info *fi)`

valid 标志了能够设置的属性，attr 记录了将要设置属性的值。这个函数会根据 valid 字段对有效的属性按照 attr 中的内容进行设置。
valid 字段的所有情况如下：
```c
#define FATTR_MODE	(1 << 0)
#define FATTR_UID	(1 << 1)
#define FATTR_GID	(1 << 2)
#define FATTR_SIZE	(1 << 3)
#define FATTR_ATIME	(1 << 4)
#define FATTR_MTIME	(1 << 5)
#define FATTR_FH	(1 << 6)
#define FATTR_ATIME_NOW	(1 << 7)
#define FATTR_MTIME_NOW	(1 << 8)
#define FATTR_LOCKOWNER	(1 << 9)
#define FATTR_CTIME	(1 << 10)
```

# 故障恢复
passthrough 的 FUSE 文件系统有两种 `passthrough.c` 以及 `passthrough_cr.c`，分别是正常模式以及故障恢复模式。在故障恢复模式中，需要使用共享内存来分配 `struct lo_inode, struct lo_dirp` 这些数据结构，我们在实现中是提前分配一块比较大的共享内存，随后需要分配这些数据结构时，从这个共享内存中分配未被占用的内存区域。具体的故障恢复模式中需要用到的额外的数据结构及函数在 `passthrough_cr_func.c` 中定义。