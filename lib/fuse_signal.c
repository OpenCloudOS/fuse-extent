/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  Utility functions for setting signal handlers.

  This program can be distributed under the terms of the GNU LGPLv2.
  See the file COPYING.LIB
*/
#include <fuse_signal.h>

static struct fuse_session *fuse_instance;

static void exit_handler(int sig)
{
	if (fuse_instance)
	{
		fuse_instance->exited = 1;
		if (sig <= 0)
		{
			fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] assertion error: signal value <= 0\n");
			abort();
		}
		fuse_instance->error = sig;
	}
}

static void do_nothing(int sig)
{
	(void)sig;
}

static int set_one_signal_handler(int sig, void (*handler)(int), int remove, int flags)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = remove ? SIG_DFL : handler;
	sigemptyset(&(sa.sa_mask));
	sa.sa_flags = flags;

	if (sigaction(sig, &sa, NULL) == -1)
	{
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] cannot set signal handler\n");
		return -1;
	}
	return 0;
}

int fuse_set_signal_handlers(struct fuse_session *se)
{
	if (set_one_signal_handler(SIGHUP, exit_handler, 0, 0) == -1 ||
		set_one_signal_handler(SIGINT, exit_handler, 0, 0) == -1 ||
		set_one_signal_handler(SIGQUIT, exit_handler, 0, 0) == -1 ||
		set_one_signal_handler(SIGTERM, exit_handler, 0, 0) == -1)
		return -1;

	fuse_instance = se;
	return 0;
}

void fuse_remove_signal_handlers(struct fuse_session *se)
{
	if (fuse_instance != se)
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse_remove_signal_handlers: unknown session\n");
	else
		fuse_instance = NULL;

	set_one_signal_handler(SIGHUP, exit_handler, 1,0);
	set_one_signal_handler(SIGINT, exit_handler, 1,0);
	set_one_signal_handler(SIGQUIT, exit_handler, 1,0);
	set_one_signal_handler(SIGTERM, exit_handler, 1,0);
}

int fuse_set_signal_ignore()
{
	if (set_one_signal_handler(SIGHUP, do_nothing, 0, 0) == -1 ||
		set_one_signal_handler(SIGINT, do_nothing, 0, 0) == -1 ||
		set_one_signal_handler(SIGQUIT, do_nothing, 0, 0) == -1 ||
		set_one_signal_handler(SIGTERM, do_nothing, 0, 0) == -1)
		return -1;
	return 0;
}

void fuse_remove_signal_ignore()
{
	set_one_signal_handler(SIGHUP, do_nothing, 1,0);
	set_one_signal_handler(SIGINT, do_nothing, 1,0);
	set_one_signal_handler(SIGQUIT, do_nothing, 1,0);
	set_one_signal_handler(SIGTERM, do_nothing, 1,0);
}