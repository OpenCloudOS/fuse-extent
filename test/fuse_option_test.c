#include <fuse_option.h>

#include <stdio.h>
#include <assert.h>
#include <string.h>

void test_cmd_opts(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_cmd_opts cmd;
    memset(&cmd, 0, sizeof(cmd));
    int res=parse_cmd_opts(&args, &cmd);
    if(res<0){
        printf("-1\n");
        return;
    }
    printf("cmd: %d,%d,%d,%d,%d,%d,%d,%s; unknown: %d\n",cmd.help,cmd.version,cmd.debug,cmd.foreground,cmd.multithread,cmd.clonefd,cmd.threads,cmd.mountpoint,args.argc);
    // int unknown = 0;
    // for (int i = 0; i < argc; i++)
    // {
    //     if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
    //         assert(cmd.help == 1);
    //     else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0)
    //         assert(cmd.version == 1);
    //     else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0){
    //         assert(cmd.debug == 1);
    //         assert(cmd.foreground == 1);
    //     }
    //     else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0)
    //         assert(cmd.foreground == 1);
    //     else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--multithread") == 0)
    //         assert(cmd.multithread == 1);
    //     else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--clonefd") == 0)
    //         assert(cmd.clonefd == 1);
    //     else
    // }
    free_cmd_opts(&cmd);
    free_fuse_args(&args);
}

void test_mnt_opts(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_mnt_opts mnt;
    memset(&mnt, 0, sizeof(mnt));
    int res=parse_mnt_opts(&args, &mnt);
    if(res<0){
        printf("-1\n");
        return;
    }
    printf("mnt: %d,%d,%d,%s,%s,%s; unknown: %d\n",mnt.allow_other,mnt.auto_unmount,mnt.flag,mnt.flags,mnt.fsname,mnt.subtype,args.argc);
    free_mnt_opts(&mnt);
    free_fuse_args(&args);
}

void test_conn_info(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_conn_info conn;
    memset(&conn, 0, sizeof(conn));
    int res=parse_conn_info(&args, &conn);
    if(res<0){
        printf("-1\n");
        return;
    }
    printf("conn: %d,%d,%d,%d,%d,%d; unknown: %d\n",conn.max_write,conn.max_read,conn.max_readahead,conn.max_background,conn.congestion_threshold,conn.time_gran,args.argc);
    free_fuse_args(&args);
}


int main(int argc, char *argv[])
{
    if(argc<2)
        return -1;
    if(strcmp(argv[1],"test_cmd_opts")==0)
        test_cmd_opts(argc,argv);
    else if(strcmp(argv[1],"test_mnt_opts")==0)
        test_mnt_opts(argc,argv);
    else if(strcmp(argv[1],"test_conn_info")==0)
        test_conn_info(argc,argv);
    else
        return -1;
    return 0;
}