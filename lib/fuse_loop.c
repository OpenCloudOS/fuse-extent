#include <fuse_loop.h>
#include "fuse_operation.c"

int fuse_daemonize(int foreground){
	if (!foreground) {
		int nullfd;
		int waiter[2];
		char completed;

		if (pipe(waiter)) {
			fuse_log(FUSE_LOG_ERR,"[FUSE_LOG_ERR] pipe error in fuse_daemonize: %s\n",strerror(errno));
			return -1;
		}

		switch(fork()) {
		case -1:
			fuse_log(FUSE_LOG_ERR,"[FUSE_LOG_ERR] fork error in fuse_daemonize: %s\n",strerror(errno));
			return -1;
		case 0:
			break;
		default:
			(void) read(waiter[0], &completed, sizeof(completed));
			exit(0);
		}

		if (setsid() == -1) {
			fuse_log(FUSE_LOG_ERR,"[FUSE_LOG_ERR] setsid error in fuse_daemonize: %s\n",strerror(errno));
			return -1;
		}

		(void) chdir("/");

		nullfd = open("/dev/null", O_RDWR, 0);
		if (nullfd != -1) {
			(void) dup2(nullfd, 0);
			(void) dup2(nullfd, 1);
			(void) dup2(nullfd, 2);
			if (nullfd > 2)
				close(nullfd);
		}

		/* Propagate completion of daemon initialization */
		completed = 1;
		(void) write(waiter[1], &completed, sizeof(completed));
		close(waiter[0]);
		close(waiter[1]);
	} else {
		(void) chdir("/");
	}
	return 0;
}

static fuse_req_p fuse_alloc_req(struct fuse_session *se)
{
	fuse_req_p req;

	req = (fuse_req_p )calloc(1, sizeof(struct fuse_req));
	if (req == NULL)
	{
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: unable to allocate a new memory for new request\n");
	}
	else
	{
		req->se = se;
		// req->ctr = 1;
		list_init_item(req);
		// pthread_mutex_init(&req->lock, NULL);
	}

	return req;
}

// 收到一个请求后检查是否存在这个请求对应的 interrupt 请求
static fuse_req_p check_interrupt(struct fuse_session *se,
										fuse_req_p req)
{
	fuse_req_p curr;

	// 寻找是否由对应的 interrupt 请求
	for (curr = se->int_list.next; curr != &se->int_list;
		 curr = curr->next)
	{
		if (curr->interrupted_id == req->unique)
		{
			req->interrupted = 1;
			list_del_item(struct fuse_req,curr);
			free(curr);
			return NULL;
		}
	}

	// 如果没有找到对应的 interrupt 请求
	// 每次需要删除一个最早放入队列的 interrupt 请求
	// 这样保证 int_list 不会无限增长
	curr = se->int_list.next;
	if (curr != &se->int_list)
	{
		list_del_item(struct fuse_req,curr);
		list_init_item(curr);
		return curr;
	}
	else
		return NULL;
}

static int fuse_session_receive(struct fuse_session *se, struct fuse_buf *buf, int clonefd)
{
	int err;
	ssize_t res;
	if (buf->mem == NULL)
	{
		buf->mem = malloc(se->bufsize);
		if (buf->mem == NULL)
		{
			fuse_log(FUSE_LOG_ERR,
					 "[FUSE_LOG_ERR] fuse: unable to allocate a new memory for receive buffer: %s\n", strerror(errno));
			return -ENOMEM;
		}
	}

restart:
	res = read(clonefd==-1 ? se->fd:clonefd, buf->mem, se->bufsize);
	err = errno;
	if (se->exited)
	{
		return 0;
	}
	if (res == -1)
	{
		// read 系统调用过程中被 signal 打断，重新调用
		if (err == EINTR)
		{
			goto restart;
		}

		// 文件系统已经解挂 (fusermount -u or umount) 或者 
		// connection was aborted via /sys/fs/fuse/connections/NNN/abort)
		if (err == ENODEV)
		{
			se->exited = 1;
			return 0;
		}

		// 默认为阻塞 I/O，因此不会产生 EAGAIN
		// 其他情况的错误
		fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] fuse: reading device: %s\n", strerror(errno));
		return -err;
	}

	if ((size_t)res < sizeof(struct fuse_in_header))
	{
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_ERR] fuse: short read on fuse device\n");
		return -EIO;
	}

	buf->size = res;

	return res;
}

static void fuse_session_process(struct fuse_session *se, struct fuse_buf *buf,
								 int clonefd)
{
	struct fuse_in_header *in = buf->mem;
	fuse_req_p req;
	int err;

	// 调试信息
	if (se->debug)
	{
		fuse_log(FUSE_LOG_DEBUG,
				 "[FUSE_LOG_DEBUG] unique: %llu, opcode: %s (%i), nodeid: 0x%x, insize: %zu, pid: %u\n",
				 (unsigned long long)in->unique,
				 opname((enum fuse_opcode)in->opcode), in->opcode,
				 (unsigned long long)in->nodeid, buf->size, in->pid);
	}

	// 分配一个新的请求体
	req = fuse_alloc_req(se);
	if (req == NULL)
	{
		struct fuse_out_header out = {
			.unique = in->unique,
			.error = -ENOMEM,
		};
		struct iovec iov = {
			.iov_base = &out,
			.iov_len = sizeof(struct fuse_out_header),
		};

		fuse_send_iov_msg(se, clonefd, &iov, 1);
	}
	req->unique = in->unique;
	req->ctx.uid = in->uid;
	req->ctx.gid = in->gid;
	req->ctx.pid = in->pid;
	req->fd = clonefd;

	err = EIO;
	// 当前会话未初始化，但是收到的请求却不是初始化请求
	// 或者当前会话已经初始化，却再次收到初始化请求
	if (!se->inited && in->opcode != FUSE_INIT)
	{
		goto reply_err;
	}
	else if (se->inited && in->opcode == FUSE_INIT)
	{
		goto reply_err;
	}

	err = EACCES;
	// 如果挂载点只允许挂载者访问
	// 那么只有当请求用户为挂载者或者root时才能访问
	if (!se->mo.allow_other && in->uid != se->owner && in->uid != 0)
	{
		enum fuse_opcode opcode = in->opcode;
		if (opcode != FUSE_INIT)
		{
			goto reply_err;
		}
	}

	err = ENOSYS;
	if (in->opcode >= FUSE_MAXOP || !fuse_ops[in->opcode].func)
		goto reply_err;

	// 1. 用户空间进程收到一个请求后收到对应的 int 请求
	// 2. 用户空间收到 int 请求后，再收到对应的请求
	// 这里的情况属于 2
	if (in->opcode != FUSE_INTERRUPT)
	{
		fuse_req_p intr;
		pthread_mutex_lock(&se->lock);
		intr = check_interrupt(se, req);
		if(req->interrupted)
			free(req);
		else
			list_add_item(req, se->req_list);
		pthread_mutex_unlock(&se->lock);
		if (intr)
			send_reply_err(intr,EAGAIN);
	}

	const void *inarg = (void *)&in[1];

	// if (in->opcode == FUSE_NOTIFY_REPLY)
	// {
		
	// }
	// else
	{
		fuse_ops[in->opcode].func(req, in->nodeid, inarg);
	}

	return;
reply_err:
	send_reply_err(req, err);
}

int fuse_single_session_loop(struct fuse_session *se)
{
	fuse_log(FUSE_LOG_INFO, "[FUSE_LOG_INFO] fuse: start single thread session loop\n");
	int res = 0;
	struct fuse_buf receive_buf = {
		.mem = NULL,
	};
	while (!se->exited)
	{
		res = fuse_session_receive(se, &receive_buf, -1);
		// 挂载点取消，正常退出
		if (res==0){
			fuse_log(FUSE_LOG_INFO, "[FUSE_LOG_INFO] fuse: single thread session loop end\n");
			break;
		}
		// 出现其他故障，故障恢复
		else if(res<0){
			se->exited=1;
			fuse_log(FUSE_LOG_INFO, "[FUSE_LOG_ERR] fuse: single thread session loop end due to an error: %s\n",strerror(-res));
			break;
		}
		fuse_session_process(se, &receive_buf, -1);
	}
	free(receive_buf.mem);
	if (res>0){
		res=0;
	}
	if(se->error){
		res=se->error;
	}
	fuse_session_reset(se);
	return res;
}

#define MAX_THREAD_NUM 96

struct fuse_worker_info;
struct fuse_worker
{
	struct fuse_worker *prev;
	struct fuse_worker *next;

	struct fuse_worker_info *wi;	// 指向当前一个总体的 worker_info
	pthread_t thread_id;			// 当前线程 ID

	
	struct fuse_buf receive_buf; 	// 线程独有的 buffer
	int fd;							// 记录该线程操作的 clonefd
};

struct fuse_worker_info
{
	struct fuse_session *se;			// 指向当前会话
	struct fuse_worker worker_head;
	int clonefd;						// 是否调用 ioctl(FUSE_DEV_IOC_CLONE) 复制一个 fuse_dev
	unsigned available; 				// 成功创建的线程数
	unsigned threads;					// 用户输入的线程数
	int error;							// 记录线程出错的原因
};

static int fuse_clonefd(struct fuse_session *se,struct fuse_worker* w)
{
	int masterfd = se->fd;
	int clonefd;
	const char *devname = "/dev/fuse";
	int res;

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
	clonefd = open(devname, O_RDWR | O_CLOEXEC);
	if (clonefd == -1)
	{
		fuse_log(FUSE_LOG_WARNING, "[FUSE_LOG_WARNING] fuse: failed to open %s: %s\n",
				 devname, strerror(errno));
		return -1;
	}
	if (!O_CLOEXEC)
		fcntl(clonefd, F_SETFD, FD_CLOEXEC);

	res = ioctl(clonefd, FUSE_DEV_IOC_CLONE, &masterfd);
	if (res == -1) {
		fuse_log(FUSE_LOG_ERR, "[FUSE_LOG_WARNING] fuse: failed to clone device: %s\n",
			strerror(errno));
		close(clonefd);
		return -1;
	}

	w->fd=clonefd;
	return 0;
}

static void *fuse_do_work(void *data){

	struct fuse_worker *w = (struct fuse_worker *) data;
	struct fuse_session *se=w->wi->se;
	int res;
	while (!se->exited)
	{
		
		// 持有锁或者申请动态内存未释放时不能被取消
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
		res = fuse_session_receive(se, &w->receive_buf, w->fd);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

		// 挂载点取消，正常退出
		if (res==0){
			break;
		}
		// 如果有一个线程出现故障，其他的线程均应该退出
		// 出现其他故障，故障恢复
		else if(res<0){
			se->exited=1;
			w->wi->error=res;
			fuse_log(FUSE_LOG_INFO, "[FUSE_LOG_ERR] fuse: thread %d session loop end due to an error:%s\n",w->thread_id,strerror(-res));
			break;
		}
		fuse_session_process(se, &w->receive_buf, w->fd);
	}

	if(res>=0){
		fuse_log(FUSE_LOG_INFO, "[FUSE_LOG_INFO] fuse: thread %d session loop end\n",w->thread_id);
	}
	
	return NULL;
}

static int fuse_create_thread(pthread_t *thread_id, void *(*func)(void *), void *arg)
{
	sigset_t oldset;
	sigset_t newset;
	int res;

	/* Disallow signal reception in worker threads */
	sigemptyset(&newset);
	sigaddset(&newset, SIGTERM);
	sigaddset(&newset, SIGINT);
	sigaddset(&newset, SIGHUP);
	sigaddset(&newset, SIGQUIT);
	pthread_sigmask(SIG_BLOCK, &newset, &oldset);
	res = pthread_create(thread_id, NULL, func, arg);
	pthread_sigmask(SIG_SETMASK, &oldset, NULL);
	if (res != 0) {
		fuse_log(FUSE_LOG_WARNING, "[FUSE_LOG_WARNING] fuse: error creating thread: %s\n",
			strerror(res));
		return -1;
	}

	return 0;
}

static void fuse_join_thread(struct fuse_worker *w)
{
	pthread_cancel(w->thread_id);
	pthread_join(w->thread_id, NULL);
	list_del_item(struct fuse_worker,w);
	free(w->receive_buf.mem);
	free(w);
}

int fuse_multi_session_loop(struct fuse_session *se, int clonefd, unsigned threads)
{
	int *clonefds=NULL;
	if(clonefd){
		clonefds=(int *)calloc(threads+1,sizeof(int));
		if (clonefds==NULL){
			fuse_log(FUSE_LOG_ERR,
				 "[FUSE_LOG_ERR] cannot init fuse_crash_recovery: %s", strerror(errno));
			return -ERECOVERY;
		}
		clonefds[threads]=NULL;
	}

	struct fuse_worker_info wi;
	FUSE_LIST_INIT(wi.worker_head);
	wi.clonefd = clonefd;
	wi.available = 0;
	wi.error=0;
	wi.se=se;
	if (threads > MAX_THREAD_NUM)
	{
		fuse_log(FUSE_LOG_WARNING,
				 "[FUSE_LOG_WARNING] too many threads created, input:%u, maximum:%u", threads, MAX_THREAD_NUM);
		wi.threads = MAX_THREAD_NUM;
	}
	else
	{
		wi.threads = threads;
	}
	unsigned i;
	int res=0;
	for (i=0 ; i < wi.threads; i++)
	{
		struct fuse_worker* w=(struct fuse_worker*)malloc(sizeof(struct fuse_worker));
		w->fd=-1;
		w->wi=&wi;
		if(w==NULL){
			fuse_log(FUSE_LOG_WARNING,
				"[FUSE_LOG_WARNING] fuse: unable to allocate a new memory for fuse_worker: %s\n", strerror(errno));
			continue;
		}

		if(clonefd){
			fuse_clonefd(se,w);
		}

		res=fuse_create_thread(&w->thread_id,fuse_do_work,w);
		if (res<0){
			free(w);
		}else{
			wi.available++;
			list_add_item(w,wi.worker_head);
		}
	}
	if(wi.available==0){
		fuse_log(FUSE_LOG_ERR,
				"[FUSE_LOG_ERR] fuse: cannot create any threads to handle requests from user\n");
		res=-ENOTHREAD;
	}else{
		fuse_log(FUSE_LOG_INFO,
				"[FUSE_LOG_INFO] fuse: %u/%u is(are) running running to handle requests from user\n",wi.available,wi.threads);
		while (!se->exited)
			sleep(1);
		size_t index=0;
		struct fuse_worker* w;
		while (wi.worker_head.next!=&wi.worker_head){
			w=wi.worker_head.next;
			if(clonefd&&w->fd!=-1)
				clonefds[index++]=w->fd;
			fuse_join_thread(w);
		}
		se->clonefds=clonefds;
		res=0;
		if (wi.error)
			res=wi.error;
		if(se->error)
			res=se->error;
	}
	fuse_session_reset(se);
	return res;
}