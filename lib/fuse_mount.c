#include <fuse_mount.h>

// 该函数通过系统调用进行挂载
// 挂载成功返回打开的 /dev/fuse 文件对应的文件描述符；挂载失败返回 -1
// 如果因为系统不支持 unprivileged mount 而挂载失败，则返回 -2
static int fuse_mount_sys(const char *mountpoint, const struct fuse_mnt_opts mo)
{
	// fuse_mount_sys 不支持 auto_unmount
	// 如果需要 auto_unmount，那么应该使用 fuse_mount_fusermount
	if (mo.auto_unmount)
	{
		return -2;
	}
	const char *devname = "/dev/fuse";
	int fd;
	int res;

	struct stat stbuf;
	res = stat(mountpoint, &stbuf);
	if (res == -1)
	{
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: failed to access mountpoint %s: %s\n",
				 mountpoint, strerror(errno));
		return -1;
	}

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
	fd = open(devname, O_RDWR | O_CLOEXEC);
	if (fd == -1)
	{
		if (errno == ENODEV || errno == ENOENT)
			fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: /dev/fuse not found, try 'modprobe fuse' first\n");
		else
			fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: failed to open %s: %s\n",
					 devname, strerror(errno));
		return -1;
	}
	if (!O_CLOEXEC)
		fcntl(fd, F_SETFD, FD_CLOEXEC);

	char kernel_opts[128];
	if (mo.allow_other)
	{
		snprintf(kernel_opts, sizeof(kernel_opts), "allow_other,fd=%i,rootmode=%o,user_id=%u,group_id=%u",
				 fd, stbuf.st_mode & S_IFMT, getuid(), getgid());
	}
	else
	{
		snprintf(kernel_opts, sizeof(kernel_opts), "fd=%i,rootmode=%o,user_id=%u,group_id=%u",
				 fd, stbuf.st_mode & S_IFMT, getuid(), getgid());
	}

	char *type = malloc((mo.subtype ? strlen(mo.subtype) : 0) + 32);
	char *source = malloc((mo.fsname ? strlen(mo.fsname) : 0) +
						  (mo.subtype ? strlen(mo.subtype) : 0) +
						  strlen(devname) + 32);
	strcpy(type, "fuse");
	if (mo.subtype)
	{
		strcat(type, ".");
		strcat(type, mo.subtype);
	}
	strcpy(source,
		   mo.fsname ? mo.fsname : (mo.subtype ? mo.subtype : devname));

	if (!type || !source)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: unable to allocate a new memory: %s\n", strerror(errno));
		goto out_close;
	}

	res = mount(source, mountpoint, type, mo.flag, kernel_opts);

	// FUSE内核可能不支持subtype
	// 重新设置挂载参数 (type 只能为 "fuse", subtype 记录到 source)
	if (res == -1 && errno == ENODEV && mo.subtype)
	{
		strcpy(type, "fuse");
		if (mo.fsname)
		{
			sprintf(source, "%s#%s", mo.fsname, mo.subtype);
		}
		else
		{
			strcpy(source, mo.subtype);
		}
		res = mount(source, mountpoint, type, mo.flag, kernel_opts);
	}
	if (res == -1)
	{
		// 系统不支持 unprivileged mount (系统只有在root权限下才能执行mount)
		// 这个时候只能借助 fusermount 来进行挂载
		if (errno == EPERM)
		{
			res = -2;
		}
		else
		{
			fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: mount failed: %s\n", strerror(errno));
		}
		goto out_close;
	}

	free(type);
	free(source);
	return fd;
out_close:
	free(type);
	free(source);
	close(fd);
	return res;
}

// 这个函数由 `fuse_mount_fusermount()` 调用
// 挂载成功返回对应 /dev/fuse 的文件描述符，失败返回 -1
static int receive_fd(int fd)
{
	struct msghdr msg;
	struct iovec iov;
	char buf[1];
	int rv;
	size_t ccmsg[CMSG_SPACE(sizeof(int)) / sizeof(size_t)];
	struct cmsghdr *cmsg;

	iov.iov_base = buf;
	iov.iov_len = 1;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	/* old BSD implementations should use msg_accrights instead of
	 * msg_control; the interface is different. */
	msg.msg_control = ccmsg;
	msg.msg_controllen = sizeof(ccmsg);

	while (((rv = recvmsg(fd, &msg, 0)) == -1) && errno == EINTR)
		;
	if (rv == -1)
	{
		perror("recvmsg");
		return -1;
	}
	if (!rv)
	{
		/* EOF */
		return -1;
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg->cmsg_type != SCM_RIGHTS)
	{
		fuse_log(FUSE_LOG_ERR, "got control message of unknown type %d\n",
				 cmsg->cmsg_type);
		return -1;
	}
	return *(int *)CMSG_DATA(cmsg);
}

// 该函数通过 fusermount 程序进行挂载
// 只有在 `fuse_mount_sys()` 返回 -2 时才会调用该函数再次尝试挂载
// 挂载成功返回打开的 /dev/fuse 文件对应的文件描述符挂载失败返回 -1
static int fuse_mount_fusermount(const char *mountpoint, struct fuse_mnt_opts mo)
{
	char *fusermount_opts = malloc(strlen(mo.flags) + (mo.fsname ? strlen(mo.fsname) : 0) +
								   (mo.subtype ? strlen(mo.subtype) : 0) + 32);
	if (fusermount_opts == NULL)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: unable to allocate a new memory: %s\n", strerror(errno));
		return -1;
	}
	memset(fusermount_opts, 0, strlen(fusermount_opts));
	strcat(fusermount_opts, mo.flags);
	if (mo.allow_other)
	{
		strcat(fusermount_opts, ",allow_other");
	}
	if (mo.auto_unmount)
	{
		strcat(fusermount_opts, ",auto_unmount");
	}
	if (mo.fsname)
	{
		strcat(fusermount_opts, ",fsname=");
		strcat(fusermount_opts, mo.fsname);
	}
	if (mo.subtype)
	{
		strcat(fusermount_opts, ",subtype=");
		strcat(fusermount_opts, mo.subtype);
	}

	int fds[2], pid;
	int res;
	int rv;

	res = socketpair(PF_UNIX, SOCK_STREAM, 0, fds);
	if (res == -1)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: `socketpair()` failed: %s\n", strerror(errno));
	}

	pid = fork();
	if (pid == -1)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: `fork()` failed: %s\n", strerror(errno));
		close(fds[0]);
		close(fds[1]);
		return -1;
	}

	if (pid == 0)
	{
		char env[10];
		const char *argv[32];
		int i = 0;

		argv[i++] = FUSERMOUNT_PROG;
		if (strlen(fusermount_opts) > 0)
		{
			argv[i++] = "-o";
			argv[i++] = fusermount_opts;
		}
		argv[i++] = "--";
		argv[i++] = mountpoint;
		argv[i++] = NULL;

		close(fds[1]);
		fcntl(fds[0], F_SETFD, 0);
		snprintf(env, sizeof(env), "%i", fds[0]);
		setenv(FUSE_COMMFD_ENV, env, 1);
		execvp(FUSERMOUNT_PROG, (char **)argv);
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] failed to exec fusermount during mount: %s\n", strerror(errno));
		exit(1);
	}

	close(fds[0]);
	rv = receive_fd(fds[1]);
	free(fusermount_opts);

	// 子进程将会一直存在直到父进程的读端口关闭
	// 如果没有设置 auto_unmount，那么就将父进程的读端口关闭，使得子进程能够退出
	// 如果设置了 auto_unmount，那么不能将父进程的读端口关闭（而是等到父进程退出后关闭读端口），
	// 否则会导致文件系统自动解除挂载
	if (!mo.auto_unmount)
	{
		close(fds[1]);
		waitpid(pid, NULL, 0);
	}

	if (rv >= 0)
		fcntl(rv, F_SETFD, FD_CLOEXEC);

	return rv;
}

// 该函数首先调用 `fuse_mount_sys()` 进行挂载
// 如果因为系统不支持 unprivileged mount 而挂载失败，则调用 `fuse_mount_fusermount()` 再次进行挂载
// 最终，挂载成功返回打开的 /dev/fuse 文件对应的文件描述符；挂载失败返回 -1
static int fuse_kern_mount(const char *mountpoint, struct fuse_mnt_opts mo)
{
	int res = -1;
	res = fuse_mount_sys(mountpoint, mo);
	if (res == -2)
		res = fuse_mount_fusermount(mountpoint, mo);
	return res;
}

// 如果文件系统已经解除挂载或者被抛弃，则直接返回
// 否则，首先尝试通过系统调用 unmount() 进行解挂
// 尝试失败，则通过 fuserunmount 进行解挂
static void fuse_kern_unmount(const char *mountpoint, int fd)
{
	int res;
	int pid;

	if (fd != -1)
	{
		struct pollfd pfd;

		pfd.fd = fd;
		pfd.events = 0;
		res = poll(&pfd, 1, 0);

		// Need to close file descriptor, otherwise synchronous umount
		// would recurse into filesystem, and deadlock.

		// Caller expects fuse_kern_unmount to close the fd, so close it
		// anyway.
		close(fd);

		// 如果返回 POLLERR，文件系统已经解挂或者 connection 被抛弃 (connection
		// was aborted via /sys/fs/fuse/connections/NNN/abort)
		if (res == 1 && (pfd.revents & POLLERR))
			return;
	}

	res = umount2(mountpoint, 2);
	if (res == 0)
		return;

	pid = fork();
	if (pid == -1)
		return;

	if (pid == 0)
	{
		const char *argv[] = {FUSERMOUNT_PROG, "-u", "-q", "-z",
							  "--", mountpoint, NULL};
		execvp(FUSERMOUNT_PROG, (char **)argv);
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] failed to exec fusermount during unmount: %s\n", strerror(errno));
		exit(1);
	}
	waitpid(pid, NULL, 0);
}

int fuse_session_mount(struct fuse_session *se, const char *mountpoint)
{
	int fd;

	/*
	 * Make sure file descriptors 0, 1 and 2 are open, otherwise chaos
	 * would ensue.
	 */
	do
	{
		fd = open("/dev/null", O_RDWR);
		if (fd > 2)
			close(fd);
	} while (fd >= 0 && fd <= 2);

	fd = fuse_kern_mount(mountpoint, se->mo);
	if (fd == -1)
		return -1;
	se->fd = fd;

	se->mountpoint = mountpoint;

	return 0;
}

void fuse_session_unmount(struct fuse_session *se)
{
	if (se->clonefds!=NULL)
	{
		int *clonefd = se->clonefds;
		while (clonefd != NULL)
		{
			close(*clonefd);
			clonefd++;
		}
		free(se->clonefds);
		se->clonefds=NULL;
	}
	if (se->mountpoint != NULL)
	{
		fuse_kern_unmount(se->mountpoint, se->fd);
		se->fd = -1;
		se->mountpoint = NULL;
	}
}