#include <fuse_option.h>

#include <fuse_log.h>

static const struct fuse_opt fuse_cmd_helper_opts[] = {
    DEFINE_FUSE_OPT("-h", struct fuse_cmd_opts, help),
    DEFINE_FUSE_OPT("--help", struct fuse_cmd_opts, help),
    DEFINE_FUSE_OPT("-v", struct fuse_cmd_opts, version),
    DEFINE_FUSE_OPT("--version", struct fuse_cmd_opts, version),
    DEFINE_FUSE_OPT("-d", struct fuse_cmd_opts, debug),
    DEFINE_FUSE_OPT("--debug", struct fuse_cmd_opts, debug),
    DEFINE_FUSE_OPT("-f", struct fuse_cmd_opts, foreground),
    DEFINE_FUSE_OPT("--foreground", struct fuse_cmd_opts, foreground),
    DEFINE_FUSE_OPT("-m", struct fuse_cmd_opts, multithread),
    DEFINE_FUSE_OPT("--multithread", struct fuse_cmd_opts, multithread),
	DEFINE_FUSE_OPT("-c", struct fuse_cmd_opts, clonefd),
    DEFINE_FUSE_OPT("--clonefd", struct fuse_cmd_opts, clonefd),
    DEFINE_FUSE_OPT("-t=%u", struct fuse_cmd_opts, threads),
    DEFINE_FUSE_OPT("--threads=%u", struct fuse_cmd_opts, threads),
    FUSE_OPT_END
};

static const struct fuse_opt fuse_mnt_helper_opts[] = {
    DEFINE_FUSE_OPT("--allow_other", struct fuse_mnt_opts ,allow_other),
	DEFINE_FUSE_OPT("--auto_unmount", struct fuse_mnt_opts ,auto_unmount),
    DEFINE_FUSE_OPT("--flags=%s", struct fuse_mnt_opts ,flags),
    DEFINE_FUSE_OPT("--fsname=%s", struct fuse_mnt_opts ,fsname),
    DEFINE_FUSE_OPT("--subtype=%s", struct fuse_mnt_opts ,subtype),
	FUSE_OPT_END
};

struct mount_flags {
	const char *opt;
	unsigned long flag;
	int on;
};

static const struct mount_flags mount_flags[] = {
	{"rw",	    MS_RDONLY,	    0},
	{"ro",	    MS_RDONLY,	    1},
	{"suid",    MS_NOSUID,	    0},
	{"nosuid",  MS_NOSUID,	    1},
	{"dev",	    MS_NODEV,	    0},
	{"nodev",   MS_NODEV,	    1},
	{"exec",    MS_NOEXEC,	    0},
	{"noexec",  MS_NOEXEC,	    1},
	{"async",   MS_SYNCHRONOUS, 0},
	{"sync",    MS_SYNCHRONOUS, 1},
	{"atime",   MS_NOATIME,	    0},
	{"noatime", MS_NOATIME,	    1},
	{NULL,	    0,		    0}
};

static const struct fuse_opt fuse_conn_info_helper[] = {
    DEFINE_FUSE_OPT("--max_write=%u", struct fuse_conn_info ,max_write),
	DEFINE_FUSE_OPT("--max_read=%u", struct fuse_conn_info ,max_read),
    DEFINE_FUSE_OPT("--max_readahead=%u", struct fuse_conn_info ,max_readahead),
    DEFINE_FUSE_OPT("--max_background=%u", struct fuse_conn_info ,max_background),
    DEFINE_FUSE_OPT("--congestion_threshold=%u", struct fuse_conn_info ,congestion_threshold),
	DEFINE_FUSE_OPT("--time_gran=%u", struct fuse_conn_info ,time_gran),
	FUSE_OPT_END
};

// 字符串 flags 解析为数值 flag，并加上默认选项 MS_NOSUID|MS_NODEV
static int parse_flags(const char *flags){
	int flag=MS_NOSUID|MS_NODEV;
	if (flags==NULL){
		return flag;
	}
	size_t arglen=strlen(flags);
	char temp[arglen+1];
	size_t index=0;
	size_t i=0;
	const struct mount_flags* mnt_flags;
	while (index<=arglen){
		if (flags[index]!=','&&flags[index]!='\0'){
			temp[i++]=flags[index];
		}else{
			temp[i]='\0';
			i=0;
			for(mnt_flags=mount_flags;mnt_flags&&mnt_flags->opt!=NULL;mnt_flags++){
				if (strcmp(temp,mnt_flags->opt)==0){
					if(mnt_flags->on){
						flag|=mnt_flags->flag;
					}else{
						flag&=~mnt_flags->flag;
					}
					break;
				}
			}
			if (mnt_flags->opt==NULL){
				fuse_log(FUSE_LOG_WARNING,
						"[FUSE_LOG_WARNING] invalid flag `%s`\n",temp);
			}
		}
		index++;
	}
	return flag;
}

// 数值 flag 解析为字符串 flags，这个过程会去除重复的标志
static void parse_flag(int flag,char *flags){
	memset(flags,0,strlen(flags));
	const struct mount_flags *p;
	for (p=mount_flags;p&&p->opt;p+=2){
		if(flag&(p+1)->flag){
			strcat(flags,(p+1)->opt);
		}else{
			strcat(flags,p->opt);
		}
		strcat(flags,",");
	}
	flags[strlen(flags)-1]='\0';
}

// 寻找 opts 静态数组中，str 字段与 arg 相匹配的项并返回
// 如果 str 字段为 -o=% 或者 --opt=% 类型，sepp 被设置为 '=' 所在的位置
static const struct fuse_opt *find_opt(const struct fuse_opt *opt,
									   const char *arg, unsigned *sepp)
{
	for (; opt && opt->str; opt++)
	{
		const char *str = opt->str;
		const char *sep = strchr(str, '=');
		if (sep)
		{
			size_t toffset = sep - str;
			size_t tlen = toffset + 1;
			size_t arglen = strlen(arg);
			if (arglen > tlen && strncmp(str, arg, tlen) == 0)
			{
				*sepp = toffset;
				return opt;
			}
		}
		else
		{
			if (strcmp(str, arg) == 0)
			{
				*sepp = 0;
				return opt;
			}
		}
	}
	return NULL;
}

// 如果 str 字段为 -o=% 或者 --opt=% 类型，解析 '=' 后面对应的值，并根据解析的结果设置 data 相应的字段
static int parse_value(void *data, const struct fuse_opt *opt, const char *arg, unsigned sepp)
{
	const char *value = arg + sepp + 1;
	if (value == NULL)
	{
		return -1;
	}
	char *tmp;
	switch (opt->str[sepp + 2])
	{
	case 'u':
		if (strspn(value, "0123456789")==strlen(value)){
			*(unsigned *)((char *)data + opt->offset) = (unsigned)atoi(value);
			return 0;
		}
		else{
			fuse_log(FUSE_LOG_ERR,"[FUSE_LOG_ERR] expect a valid number, but receive %s\n",value);
			return -1;
		}
	case 's':
		tmp=strdup(value);
		if(tmp==NULL)
			return -1;
		*(char **)((char *)data + opt->offset)=tmp;
		return 0;
	default:
		return -1;
	}
}

int fuse_opts_parse(struct fuse_args *args, void *data,
					const struct fuse_opt opts[])
{
	struct fuse_args out_args;
	out_args.argc = 0;
	out_args.argv = (char **)malloc(args->argc * sizeof(char *));
	if (out_args.argv == NULL)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: unable to allocate a new memory: %s\n", strerror(errno));
		return -1;
	}
	out_args.allocated = 1;

	const struct fuse_opt *opt;
	unsigned sepp;
	int i;
	for (i = 0; i < args->argc; i++)
	{
		opt = find_opt(opts, args->argv[i], &sepp);
		// 如果参数属于 opts 中的参数
		if (opt)
		{
			// 如果 option 是 --opt 或者 -o 类型的
			if (sepp == 0)
			{
				*(int *)((char *)data + opt->offset) = opt->value;
			}
			//如果 optrion 是 --opt= 或者 -o= 类型的
			else
			{
				int ret = parse_value(data, opt, args->argv[i], sepp);
				if (ret < 0)
				{
					fuse_log(FUSE_LOG_ERR,
							"[FUSE_LOG_ERR] fuse: unable to parse the value (invalid argument)\n");
					goto err_out;
				}
			}
		}
		// 否则，当前 opts 不能解析这个参数
		else
		{
			char *dupstr = strdup(args->argv[i]);
			if (dupstr == NULL)
			{
				fuse_log(FUSE_LOG_ERR,
						 "[FUSE_LOG_ERR] fuse: unable to allocate a new memory: %s\n", strerror(errno));
				goto err_out;
			}
			out_args.argv[out_args.argc++] = dupstr;
		}
	}
	free_fuse_args(args);
	*args = out_args;
	return 0;
err_out:
	free_fuse_args(&out_args);
	return -1;
}

void free_fuse_args(struct fuse_args *args)
{
	if (args)
	{
		if (args->argv && args->allocated)
		{
			int i;
			for (i = 0; i < args->argc; i++)
				free(args->argv[i]);
			free(args->argv);
		}
		args->argc = 0;
		args->argv = NULL;
		args->allocated = 0;
	}
}

int parse_cmd_opts(struct fuse_args *args, struct fuse_cmd_opts *opts)
{
	int ret = fuse_opts_parse(args, opts, fuse_cmd_helper_opts);
	if (ret < 0 || args->argc == 0)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: inappropriate arugment passing to `parse_cmd_opts()`\n");
		return -1;
	}

	// 调试模式下，进程需要在前端运行
	if (opts->debug)
		opts->foreground=1;

	//路径参数解析为绝对路径
	char *path = args->argv[args->argc - 1];
	char abspath[PATH_MAX] = "";
	if (realpath(path, abspath) == NULL)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: bad mount point `%s`: %s\n",
				 path, strerror(errno));
		return -1;
	}
	opts->mountpoint = realloc(opts->mountpoint, strlen(abspath) + 1);
	if (opts->mountpoint == NULL)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: unable to allocate a new memory: %s\n", strerror(errno));
		return -1;
	}
	strcpy(opts->mountpoint, abspath);
	free(args->argv[args->argc - 1]);
	args->argc--;
	return 0;
}

void free_cmd_opts(struct fuse_cmd_opts *opts){
	if(opts->mountpoint!=NULL){
		free(opts->mountpoint);
		opts->mountpoint=NULL;
	}
}

int parse_mnt_opts(struct fuse_args *args, struct fuse_mnt_opts *opts){
	int ret = fuse_opts_parse(args, opts, fuse_mnt_helper_opts);
	if (ret < 0 ||	args->argc==0)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: inappropriate arugment passing to `parse_mnt_opts()`\n");
		return -1;
	}
	opts->flag=parse_flags(opts->flags);
	if (opts->flags==NULL)
		opts->flags=malloc(64);
	else
		opts->flags=realloc(opts->flags,strlen(opts->flags)+32);
	if(opts->flags==NULL){
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: unable to allocate a new memory: %s\n", strerror(errno));
		return -1;	
	}
	parse_flag(opts->flag,opts->flags);
	return 0;
}

void free_mnt_opts(struct fuse_mnt_opts *opts){
	if(opts->flags!=NULL){
		free(opts->flags);
		opts->flags=NULL;
	}
	if(opts->fsname!=NULL){
		free(opts->fsname);
		opts->fsname=NULL;
	}
	if(opts->subtype!=NULL){
		free(opts->subtype);
		opts->subtype=NULL;
	}
}

int parse_conn_info(struct fuse_args *args, struct fuse_conn_info *info){
	int ret = fuse_opts_parse(args, info, fuse_conn_info_helper);
	if (ret < 0 ||	args->argc==0)
	{
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: inappropriate arugment passing to `parse_conn_info()`\n");
		return -1;
	}
	if(info->time_gran<1){
		fuse_log(FUSE_LOG_WARNING,
				 "[FUSE_LOG_WARNING] fuse: the minimum time granularity is 1 (ns)\n");
		info->time_gran=1;
		return 0;
	}
	unsigned time_gran=1;
	while (time_gran<info->time_gran)
		time_gran*=10;
	if(info->time_gran!=time_gran){
		fuse_log(FUSE_LOG_WARNING,
				 "[FUSE_LOG_WARNING] fuse: the time granularity should be power of 10 (ns)\n");
		info->time_gran=time_gran;
	}
	return 0;
}

void fuse_help()
{
	printf("progma [-h,--help] [option] <mountpoint>\n");
}

void fuse_cmd_help()
{
	printf("fuse cmdline options: \n");
	printf("    [-h, --help]                 print help\n"
		   "    [-v, --version]              print version\n"
		   "    [-d, --debug]                enable debug output\n"
		   "    [-f, --foreground]           foreground operation\n"
		   "    [-m, --multithread]          enable multi-thread operation\n"
		   "    [-c, --clonefd]              clone /dev/fuse for every thread under multithread mode\n"
		   "    [-t, --threads=%%u]           number of worker threads in multi-thread mode (default=10)\n");
}

void fuse_mnt_help()
{
	printf("fuse mount options: \n");
	printf("    [--allow_other]              allow access by all users\n"
		   "    [--auto_unmount]             auto unmount the filesystem when the daemonize end (this option only works in fusermount)\n"
		   "    [--fsname=%%s]                name of the mounted FUSE filesystem\n"
		   "    [--subtype=%%s]               type=fuse.${subtype}\n"
		   "    [--flags=<flag[,flag]...>]   flag for mount the filesystem (default=nosuid,nodev)\n"
		   "    Available flags: rw, ro, suid, nosuid, dev, nodev, exec, noexec, async, sync, atime, noatime\n");
}

void fuse_conn_help(){
	printf("fuse connection init options: \n");
	printf("    [--max_write=%%u]             maximum number of written data (default=UINT_MAX)\n"
		   "    [--max_read=%%u]              maximum number of read data (default=0)\n"
		   "    [--max_readahead=%%u]         maximum readahead (default=4)\n"
		   "    [--max_background=%%u]        maximum readahead (default=4)\n"
		   "    [--congestion_threshold=%%u]  congestion threshold (default=3)\n"
		   "    [--time_gran=%%u]             time granularity (ns) of the filesystem (default=1)\n");	
}