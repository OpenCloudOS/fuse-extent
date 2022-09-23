#include <fuse_signal.h>

#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>

#include <sys/mman.h>
#include <sys/wait.h>

void test_sighup(pid_t pid,struct fuse_session* se){
    kill(pid,SIGHUP);
    sleep(1);
    assert(se->exited==1);
    se->exited=0;
}

void test_sigint(pid_t pid,struct fuse_session* se){
    kill(pid,SIGINT);
    sleep(1);
    assert(se->exited==1);
    se->exited=0;
}

void test_sigterm(pid_t pid,struct fuse_session* se){
    kill(pid,SIGTERM);
    sleep(1);
    assert(se->exited==1);
    se->exited=0;
}

void test_sigpipe(pid_t pid,struct fuse_session* se){
    kill(pid,SIGPIPE);
    sleep(1);
}

int main(){
    struct fuse_session* se=(struct fuse_session *)mmap(NULL, sizeof(struct fuse_session), PROT_WRITE|PROT_READ, MAP_SHARED|MAP_ANONYMOUS, -1, 0);;

    int res=fuse_set_signal_handlers(se);
    if(res<0)
        return -1;
    int pid;
    if((pid=fork())<0){
        return -1;
    }
    
    // 子进程会复制父进程的 signal_handler
    // 子进程
    if(pid==0){
        struct sigaction old_sa;
        if(sigaction(SIGPIPE, NULL, &old_sa) == -1)
            return -1;
        printf("[child] %p\n",old_sa.__sigaction_handler);
        for(;;);
    }
    // 父进程
    else{
        struct sigaction old_sa;
        if(sigaction(SIGPIPE, NULL, &old_sa) == -1)
            return -1;
        printf("[parent] %p\n",old_sa.__sigaction_handler);
        test_sighup(pid,se);
        test_sigint(pid,se);
        test_sigterm(pid,se);
        test_sigpipe(pid,se);
        kill(pid,SIGKILL);
        wait(NULL);
    }
    return 0;
}