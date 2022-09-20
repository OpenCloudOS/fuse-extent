#include <fuse_mount.h>
#include <assert.h>

int main(int argc,char *argv[]){
    char* mountpoint="/root/fuse-extent/fusedir/testdir";
    struct fuse_args args=FUSE_ARGS_INIT(argc,argv);
    struct fuse_ops ops;
    struct fuse_session *se=fuse_session_new(&args,&ops,0,NULL);
    assert(se!=NULL);
    int res=fuse_session_mount(se,mountpoint);
    assert(res!=-1);
    fuse_session_unmount(se);
    fuse_session_destroy(se);
    free_fuse_args(&args);
    return 0;
}