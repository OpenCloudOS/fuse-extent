#include <fuse_session.h>

struct fuse_session *fuse_session_new(struct fuse_args *args, const struct fuse_ops *ops, int debug, void* userdata)
{
	int err;
	struct fuse_session *se;

	if (args->argc == 0) {
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: empty arugment passed to `fuse_session_new()`\n");
		return NULL;
	}

	se=(struct fuse_session *)malloc(sizeof(struct fuse_session));
	if(se==NULL){
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: unable to allocate fuse_session object: %s\n",strerror(errno));
		return NULL;
	}
	memset(se,0,sizeof(struct fuse_session));
	se->fd=-1;
	se->clonefds=NULL;
	se->debug=debug;

	struct fuse_conn_info conn=FUSE_CONN_INFO_INIT;
	struct fuse_mnt_opts mo=FUSE_MNT_OPTS_INIT;
	err=parse_mnt_opts(args,&mo);
	if(err<0){
		goto err_out;
	}
	err=parse_conn_info(args,&conn);
	if(err<0){
		goto err_out;
	}
	if(args->argc==1){
		if (args->argv[0][0]=='-'){
			fuse_log(FUSE_LOG_WARNING, "[FUSE_LOG_WARNING] fuse: argv[0] looks like an option, but "
			"will be ignored\n");
		}else{
			char *program=strrchr(args->argv[0],'/');
			if (program==NULL){
				fuse_log(FUSE_LOG_WARNING, "[FUSE_LOG_WARNING] fuse: cannot detect the name of running program\n");
			}else{
				program++;
				if(mo.fsname==NULL){
					mo.fsname=strdup(program);
				}
				if(mo.subtype==NULL){
					mo.subtype=strdup(program);
				}
			}
		}
	}else if(args->argc!=1){
		int i;
		fuse_log(FUSE_LOG_WARNING, "[FUSE_LOG_WARNING] fuse: unknown option(s): `");
		for(i = 1; i < args->argc-1; i++)
			fuse_log(FUSE_LOG_WARNING, "%s ", args->argv[i]);
		fuse_log(FUSE_LOG_WARNING, "%s`\n", args->argv[i]);
	}
	
	se->bufsize = FUSE_MAX_MAX_PAGES * getpagesize() +
		FUSE_BUFFER_HEADER_SIZE;

	FUSE_LIST_INIT(se->req_list);
	FUSE_LIST_INIT(se->int_list);
	//FUSE_LIST_INIT(se->notify_list);
	//se->notify_ctr = 1;
	pthread_mutex_init(&se->lock, NULL);

	memcpy(&se->ops, ops, sizeof(struct fuse_ops));
	se->owner = getuid();
	se->userdata = userdata;
	se->mo=mo;
	se->conn=conn;
	return se;
err_out:
	free_mnt_opts(&se->mo);
	free(se);
	return NULL;
}

void fuse_session_reset(struct fuse_session *se){
	se->exited=0;
	se->error=0;
}

void fuse_session_destroy(struct fuse_session *se){
	if(se->inited&&!se->destroyed){
		se->destroyed=1;
		if(se->ops.destroy)
			se->ops.destroy(se->userdata);
	}
	pthread_mutex_destroy(&se->lock);
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
	if (se->fd != -1){
		close(se->fd);
		se->fd=-1;
	}
	if(se->mountpoint){
		se->mountpoint=NULL;
	}
	free_mnt_opts(&se->mo);
	free(se);
}