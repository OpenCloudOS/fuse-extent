#include <fuse_crash.h>

// 文件系统下最大能分配的 lo_inode 数量
#define MAX_INODE_NUM 510
// 文件系统下最大能够 open 的文件数量（除去 O_PATH 打开的文件）
#define MAX_FILEOPEN_NUM 255
// 文件系统下最大能够 opendir 的目录数量（除去 O_PATH 打开的目录）
#define MAX_DIROPEN_NUM 255

struct lo_inode_cache
{
	size_t index; // 当前分配到的 inodes号
	size_t count; // 已经占用的 inodes 数
	struct lo_inode inodes[MAX_INODE_NUM];
};

static struct lo_inode_cache *ino_cache = NULL;

static struct lo_inode *alloc_inode()
{
	assert(ino_cache != NULL);
	if (ino_cache->count == MAX_INODE_NUM)
		return NULL;
	size_t index = ino_cache->index;
	struct lo_inode *inode = NULL;
	while (1)
	{
		if (ino_cache->inodes[index].used == 0)
		{
			inode = &ino_cache->inodes[index];
			inode->used = 1;
			index = (index + 1) % MAX_INODE_NUM;
			break;
		}
		else
		{
			index = (index + 1) % MAX_INODE_NUM;
		}
	}
	ino_cache->index = index;
	ino_cache->count++;
	return inode;
}

static void free_inode(struct lo_inode *inode)
{
	assert(ino_cache != NULL);
	inode->used = 0;
	ino_cache->count--;
}

struct lo_fdmap_cache
{
	size_t index;
	size_t count;
	struct lo_fdmap fdmaps[MAX_FILEOPEN_NUM];
};

static struct lo_fdmap_cache *fdm_cache = NULL;

static int alloc_fdmap(int fh)
{
	assert(fdm_cache != NULL);
	if (fdm_cache->count == MAX_INODE_NUM)
		return -1;
	size_t index = fdm_cache->index;
	int fdmap = 0;
	while (1)
	{
		if (fdm_cache->fdmaps[index].used == 0)
		{
			fdm_cache->fdmaps[index].used = 1;
			fdm_cache->fdmaps[index].fh = fh;
			fdmap = index;
			index = (index + 1) % MAX_INODE_NUM;
			break;
		}
		else
		{
			index = (index + 1) % MAX_INODE_NUM;
		}
	}
	fdm_cache->index = index;
	fdm_cache->count++;
	return fdmap;
}

static void free_fdmap(int fdmap)
{
	assert(fdm_cache != NULL);
	fdm_cache->fdmaps[fdmap].used = 0;
	fdm_cache->count--;
}

static int parse_fdmap(int fdmap)
{

	assert(fdm_cache != NULL);
	return fdm_cache->fdmaps[fdmap].fh;
}

struct lo_dirp_cache
{
	size_t index;
	size_t count;
	struct lo_dirp dirps[MAX_DIROPEN_NUM];
};

static struct lo_dirp_cache *dir_cache = NULL;

static struct lo_dirp *alloc_dirp()
{
	assert(dir_cache != NULL);
	if (dir_cache->count == MAX_DIROPEN_NUM)
		return NULL;
	size_t index = dir_cache->index;
	struct lo_dirp *dirp = NULL;
	while (1)
	{
		if (dir_cache->dirps[index].used == 0)
		{
			dirp = &dir_cache->dirps[index];
			dirp->used = 1;
			index = (index + 1) % MAX_DIROPEN_NUM;
			break;
		}
		else
		{
			index = (index + 1) % MAX_DIROPEN_NUM;
		}
	}
	dir_cache->index = index;
	dir_cache->count++;
	return dirp;
}

static void free_dirp(struct lo_dirp *dirp)
{
	assert(dir_cache != NULL);
	dirp->used = 0;
	dir_cache->count--;
}

static int fds[2];

// 工作线程收到 lookup 请求，创建完描述符之后向故障恢复进程共享（传递消息）
// 消息格式 lookup/${address_of_lo_inode}，控制消息附带打开的文件描述符
static int pass_notify_lookup(struct lo_inode *inode)
{
	char buf[32];
	sprintf(buf, "lookup/%lu", (uintptr_t)inode);
	int sendfd = inode->fd;
#ifdef DEBUG
	fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] notify: %s/%d\n", buf, sendfd);
#endif
	return send_fd(fds[0], buf, strlen(buf), sendfd);
}

// 工作线程收到 forget 请求，关闭对应的描述符之后向故障恢复进程通知，使得故障恢复线程也关闭对应的文件描述符
// 消息格式 forget/${address_of_lo_inode}，不携带控制消息
static int pass_notify_forget(struct lo_inode *inode)
{
	char buf[32];
	sprintf(buf, "forget/%lu", (uintptr_t)inode);
#ifdef DEBUG
	fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] notify: %s\n", buf);
#endif
	return send_fd(fds[0], buf, strlen(buf), 0);
}

// 工作线程收到 open 或者 create 请求，创建完描述符之后向故障恢复进程共享（传递消息）
// 消息格式 open/${fdmap}，控制消息附带打开的文件描述符
static int pass_notify_open(int fdmap)
{
	char buf[32];
	sprintf(buf, "open/%d", fdmap);
	int sendfd = fdm_cache->fdmaps[fdmap].fh;
#ifdef DEBUG
	fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] notify: %s/%d\n", buf, sendfd);
#endif
	return send_fd(fds[0], buf, strlen(buf), sendfd);
}

// 工作线程收到 close 请求，关闭对应的描述符之后向故障恢复进程通知，使得故障恢复线程也关闭对应的文件描述符
// 消息格式 close/${fdmap}，不携带控制消息
static int pass_notify_close(int fdmap)
{
	char buf[32];
	sprintf(buf, "close/%d", fdmap);
#ifdef DEBUG
	fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] notify: %s\n", buf);
#endif
	return send_fd(fds[0], buf, strlen(buf), 0);
}

// 工作线程收到 opendir 请求，创建完描述符之后向故障恢复进程共享（传递消息）
// 消息格式 opendir/${address_of_lo_dirp}，控制消息附带打开的文件描述符
static int pass_notify_opendir(struct lo_dirp *dirp, int sendfd)
{
	char buf[32];
	sprintf(buf, "opendir/%lu", (uintptr_t)dirp);
#ifdef DEBUG
	fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] notify: %s/%d\n", buf, sendfd);
#endif
	return send_fd(fds[0], buf, strlen(buf), sendfd);
}

// 工作线程收到 closedir 请求，关闭对应的描述符之后向故障恢复进程通知，使得故障恢复线程也关闭对应的文件描述符
// 消息格式 closedir/${address_of_lo_dirp}，不携带控制消息
static int pass_notify_closedir(struct lo_dirp *dirp)
{
	char buf[32];
	sprintf(buf, "closedir/%lu", (uintptr_t)dirp);
#ifdef DEBUG
	fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] notify: %s\n", buf);
#endif
	return send_fd(fds[0], buf, strlen(buf), 0);
}

static int shmem_init()
{
	ino_cache = mmap(NULL, sizeof(struct lo_inode_cache), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	fdm_cache = mmap(NULL, sizeof(struct lo_fdmap_cache), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	fdm_cache->index = 3;
	fdm_cache->count = 3;
	fdm_cache->fdmaps[0].used = 1;
	fdm_cache->fdmaps[1].used = 1;
	fdm_cache->fdmaps[2].used = 1;
	dir_cache = mmap(NULL, sizeof(struct lo_dirp_cache), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ino_cache == NULL || fdm_cache == NULL || dir_cache == NULL)
	{
		if (ino_cache)
			munmap(ino_cache, sizeof(struct lo_inode_cache));
		if (fdm_cache)
			munmap(fdm_cache, sizeof(struct lo_fdmap_cache));
		if (dir_cache)
			munmap(dir_cache, sizeof(struct lo_dirp_cache));
		ino_cache = NULL;
		fdm_cache = NULL;
		dir_cache = NULL;
		return -1;
	}
	return 0;
}

static void shmem_destroy()
{
	if (ino_cache)
		munmap(ino_cache, sizeof(struct lo_inode_cache));
	if (fdm_cache)
		munmap(fdm_cache, sizeof(struct lo_fdmap_cache));
	if (dir_cache)
		munmap(dir_cache, sizeof(struct lo_dirp_cache));
	ino_cache = NULL;
	fdm_cache = NULL;
	dir_cache = NULL;
}

// 故障修复线程收到工作线程发送的消息之后，解析消息格式，设置备份文件描述符
static void *pass_notify_handler_routine(void *data)
{
	int recvfd;
	char buf[32];
	while (recv_fd(fds[1], buf, sizeof(buf), &recvfd) >= 0)
	{
#ifdef DEBUG
		fuse_log(FUSE_LOG_DEBUG, "[FUSE_LOG_DEBUG] notify handle: %s/%d\n", buf, recvfd);
#endif
		char *pos = strchr(buf, '/');
		if (pos == NULL)
		{
			fuse_log(FUSE_LOG_WARNING, "[FUSE_LOG_WARNING] unexpected notify: %s", buf);
			memset(buf, 0, sizeof(buf));
			continue;
		}
		unsigned len = pos - buf;
		if (strncmp(buf, "lookup", len) == 0)
		{
			uintptr_t address = strtoul(pos + 1, NULL, 10);
			struct lo_inode *inode = (struct lo_inode *)address;
			inode->backupfd = recvfd;
		}
		else if (strncmp(buf, "forget", len) == 0)
		{
			uintptr_t address = strtoul(pos + 1, NULL, 10);
			struct lo_inode *inode = (struct lo_inode *)address;
			close(inode->backupfd);
			inode->backupfd = 0;
		}
		else if (strncmp(buf, "open", len) == 0)
		{
			int fdmap = atoi(pos + 1);
			fdm_cache->fdmaps[fdmap].backupfh = recvfd;
		}
		else if (strncmp(buf, "close", len) == 0)
		{
			int fdmap = atoi(pos + 1);
			close(fdm_cache->fdmaps[fdmap].backupfh);
			fdm_cache->fdmaps[fdmap].backupfh = 0;
		}
		else if (strncmp(buf, "opendir", len) == 0)
		{
			uintptr_t address = strtoul(pos + 1, NULL, 10);
			struct lo_dirp *dirp = (struct lo_dirp *)address;
			dirp->backupdp = fdopendir(recvfd);
			if (dirp->backupdp == NULL)
				exit(1);
		}
		else if (strncmp(buf, "closedir", len) == 0)
		{
			uintptr_t address = strtoul(pos + 1, NULL, 10);
			struct lo_dirp *dirp = (struct lo_dirp *)address;
			closedir(dirp->backupdp);
			dirp->backupdp = NULL;
		}
		else
		{
			fuse_log(FUSE_LOG_WARNING, "[FUSE_LOG_WARNING] unexpected notify: %s", buf);
			exit(1);
		}
		memset(buf, 0, sizeof(buf));
	}
	exit(1);
}

static void pass_crash_recovery_func()
{
	assert(ino_cache != NULL);
	assert(fdm_cache != NULL);
	assert(dir_cache != NULL);
	size_t i;
	for (i = 0; i < MAX_INODE_NUM; i++)
	{
		if (ino_cache->inodes[i].used)
		{
			ino_cache->inodes[i].fd = ino_cache->inodes[i].backupfd;
		}
	}
	for (i = 0; i < MAX_FILEOPEN_NUM; i++)
	{
		if (fdm_cache->fdmaps[i].used)
		{
			fdm_cache->fdmaps[i].fh = fdm_cache->fdmaps[i].backupfh;
		}
	}
	for (i = 0; i < MAX_DIROPEN_NUM; i++)
	{
		if (dir_cache->dirps[i].used)
		{
			dir_cache->dirps[i].dp = dir_cache->dirps[i].backupdp;
		}
	}
}

static struct fuse_crash_recovery_handlers crhandlers = {
	.init = shmem_init,
	.destroy = shmem_destroy,
	.nhandler = pass_notify_handler_routine,
	.crfunc = pass_crash_recovery_func
};
