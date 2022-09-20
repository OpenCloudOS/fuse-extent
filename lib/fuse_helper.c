#include <fuse_helper.h>

int fuse_normal_mode(struct fuse_args *args, struct fuse_ops *ops, void *userdata, void (*helper)(void))
{
    int res = -EBUILD;
    struct fuse_cmd_opts opts = FUSE_CMD_OPTS_INIT;
    if (parse_cmd_opts(args, &opts) < 0)
        goto err_out4;

    if (opts.version)
    {
        printf("fuse-extent v" PROJECT_VERSION "\n");
    }

    if (opts.help)
    {
        fuse_help();
        fuse_cmd_help();
        fuse_mnt_help();
        fuse_conn_help();
        if (helper)
            helper();
    }

    struct fuse_session *se = fuse_session_new(args, ops, opts.debug, userdata);
    if (se == NULL)
        goto err_out4;

    if (fuse_set_signal_handlers(se) < 0)
        goto err_out3;
    if (fuse_session_mount(se, opts.mountpoint) < 0)
        goto err_out2;
    if (fuse_daemonize(opts.foreground) < 0)
        goto err_out1;

    if (opts.multithread)
    {
        res = fuse_multi_session_loop(se, opts.clonefd, opts.threads);
    }
    else
    {
        res = fuse_single_session_loop(se);
    }

err_out1:
    fuse_session_unmount(se);
err_out2:
    fuse_remove_signal_handlers(se);
err_out3:
    fuse_session_destroy(se);
err_out4:
    free_cmd_opts(&opts);
    return res;
}

// 这个函数会做以下事情：
// 1. 创建一个 fuse_session 共享内存 gse；
// 2. 执行故障恢复的 init 函数；
// 3. 创建故障恢复工作例程 nhandler；
// 4. fork
// 5. 子进程设置信号进入循环
// 6. 父进程等待子进程结束循环，如果子进程异常退出那么 i) ioctl 重置内核队列；ii) fuse_session_recovery 更新会话信息 iii) crfunc 恢复共享内存
// 7. 最后父进程取消例程，等待其结束；destroy 释放共享内存；释放 gse 共享内存
static int fuse_crash_recovery_start(struct fuse_session *se, struct fuse_cmd_opts opts, struct fuse_crash_recovery_handlers crhandlers)
{
    int res = -EBUILD;
    int err;

    struct fuse_session *gse = (void *)mmap(NULL, sizeof(struct fuse_session), PROT_WRITE | PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(gse==NULL)
        goto err_out0;
    memset(gse, 0, sizeof(struct fuse_session));
    fuse_session_set_ptr(gse);

    if(crhandlers.init&&crhandlers.init()<0)
        goto err_out1;

    pthread_t tid;
    if (crhandlers.nhandler&&pthread_create(&tid, NULL, crhandlers.nhandler, NULL) < 0)
        goto err_out2;

    int pid;
    int status;
CRASH_RECOVERY:
    pid = fork();
    if (pid < 0)
    {
        fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fork error in fuse_crash_recovery_start: %s\n", strerror(errno));
        return res;
    }
    // 子进程执行文件系统工作（执行文件系统工作）
    if (pid == 0)
    {
        if (fuse_set_signal_handlers(se) < 0)
            return res;
        if (opts.multithread)
        {
            res = fuse_multi_session_loop(se, opts.clonefd, opts.threads);
        }
        else
        {
            res = fuse_single_session_loop(se);
        }
        fuse_remove_signal_handlers(se);
        return res;
    }

restart:
    // 父进程等待子进程完成状态（执行故障恢复工作）
    res=wait(&status);
    err=errno;
    if(res==-1){
        if (err == EINTR)
		{
			goto restart;
		}else{
            fuse_log(FUSE_LOG_WARNING, "[FUSE_LOG_WARNING] start crash recovery mode error: %s\n", strerror(errno));
            return -EBUILD;
        }
    }
    // 1. 调用 exit(1) 或者 _exit(1) 退出，表示出错
    // 2. 收到预期之外的信号被打断退出
    if ((WIFEXITED(status) && (WEXITSTATUS(status) == 1)) || 
         WIFSIGNALED(status))
    {
        fuse_log(FUSE_LOG_INFO, "[FUSE_LOG_INFO] child process crash, goto crash recovery\n");
        fuse_log(FUSE_LOG_INFO, "[FUSE_LOG_INFO] start crash recovery rounte in parent process\n");
        ioctl(se->fd, FUSE_DEV_IOC_RECOVERY, 0);
        fuse_session_recovery(se);
        if (crhandlers.crfunc)
            crhandlers.crfunc();
        goto CRASH_RECOVERY;
    }
    // 子进程正常退出，那么父进程也退出
    res=0;
    fuse_log(FUSE_LOG_INFO, "[FUSE_LOG_INFO] child process exit normally\n");
    if (crhandlers.nhandler)
    {
        pthread_cancel(tid);
        pthread_join(tid, NULL);
    }

err_out2:
    if(crhandlers.destroy)
        crhandlers.destroy();
err_out1:
    munmap(gse, sizeof(struct fuse_session));
err_out0:
    return res;
}

int fuse_crash_recovery_mode(struct fuse_args *args, struct fuse_ops *ops, void *userdata, void (*helper)(void),
                             struct fuse_crash_recovery_handlers crhandlers)
{
    int res = -EBUILD;
    struct fuse_cmd_opts opts = FUSE_CMD_OPTS_INIT;
    if (parse_cmd_opts(args, &opts) < 0)
        goto err_out4;

    if (opts.version)
    {
        printf("fuse-extent v" PROJECT_VERSION "\n");
    }

    if (opts.help)
    {
        fuse_help();
        fuse_cmd_help();
        fuse_mnt_help();
        fuse_conn_help();
        if (helper)
            helper();
    }

    struct fuse_session *se = fuse_session_new(args, ops, opts.debug, userdata);
    if (se == NULL)
        goto err_out4;
    if (fuse_set_signal_ignore() < 0)
        goto err_out3;
    if (fuse_session_mount(se, opts.mountpoint) < 0)
        goto err_out2;
    if (fuse_daemonize(opts.foreground) < 0)
        goto err_out1;

    // 如果 fork 正常，子进程循环结束后从这个过程中返回，父进程等待子进程结束后从这个函数中返回 0
    // 如果 fork 异常，返回 -EBUILD
    res = fuse_crash_recovery_start(se, opts, crhandlers);

err_out1:
    fuse_session_unmount(se);
err_out2:
    fuse_remove_signal_ignore();
err_out3:
    fuse_session_destroy(se);
err_out4:
    free_cmd_opts(&opts);
    return res;
}