#include <fuse_session.h>
#include <assert.h>

int main(int argc,char* argv[]){
    struct fuse_args args=FUSE_ARGS_INIT(argc,argv);
    struct fuse_session *se;
    struct fuse_ops ops={0};

    se=fuse_session_new(&args,&ops,0,NULL);
    printf("%s,%s\n",se->mo.fsname,se->mo.subtype);
    fuse_session_destroy(se);
    assert(se->mo.fsname==NULL);
    assert(se->mo.subtype==NULL);
    return 0;
}