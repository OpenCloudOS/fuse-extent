#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/stat.h> 
#include <string.h>
#include <sys/wait.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

#ifndef O_DIRECT
#define O_DIRECT __O_DIRECT
#endif

#define NAME_SIZE  (512)
#define BUF_SIZE   (4096 * 3)  // for 3 lblock
#define ALIGN_SIZE (512)       // aligned to sector

static char * prefix;
static int fork_count = 10;
static int thread_count = 10;
typedef void *(*__start_routine) (void *);

static int remove_file (const char *filename)
{
	return unlink (filename);
}

static int create_file(const char *filename)
{
	int i;
	unsigned char *buf = NULL;
	int fd, ret = 0;

	posix_memalign ((void **)&buf, ALIGN_SIZE, BUF_SIZE);
	if (buf == NULL) {
		printf ("alloc memory failed.\n");
		return -1;
	}

	// random data
	srand((unsigned) time(NULL ));
	for (i = 0; i < BUF_SIZE; i++) {
		buf[i] = rand() % BUF_SIZE;
	}

	fd = open (filename, O_CREAT | O_DIRECT | O_RDWR | O_TRUNC, 0666);
	if (fd < 0) {
		printf ("open %s failed.\n", filename);
		free (buf);
		return -2;
	}

	ret = write (fd, buf, BUF_SIZE);
	if (ret < BUF_SIZE) {
		printf ("write size %d, but size is :%d\n", BUF_SIZE, ret);
		return -3;
	}
	printf ("write size %d\n", ret);
	sync();

	free (buf);
	close(fd);

	return 0;
}

long do_units(void)
{
	long id;
	char filename[NAME_SIZE];	

	id = pthread_self();
	snprintf (filename, NAME_SIZE, "%s_%ld", prefix, id);
	if (access (filename, F_OK) == 0) {
		remove_file (filename);
	}

	(void) create_file (filename);
	(void) remove_file (filename);

	return 0;
}

int do_task(int task_nr)
{
	int i;
	int ret;
	long thread_ret;
	pthread_t threads[thread_count];

	if (!thread_count) {
		return do_units();
	}

	for (i = 0; i < thread_count; i++) {
		ret = pthread_create(&threads[i], NULL, (__start_routine) do_units,
  							 NULL);
		if (ret) {
			perror("pthread_create");
			exit(1);
		}
	}

	for (i = 0; i < thread_count; i++) {
		ret = pthread_join(threads[i], (void *)&thread_ret);
		if (ret) {
			perror("pthread_join");
			exit(1);
		}
	}

	return 0;
}

static int do_tasks(void)
{
	int i;
	int status;
	int child_pid;

	if (!fork_count) {
		return do_task(0);
	}

	for (i = 0; i < fork_count; i++) {
		if ((child_pid = fork()) == 0)
			return do_task(i);
		else if (child_pid < 0)
			fprintf(stderr, "failed to fork: %s\n",
				strerror(errno));
	}

	for (i = 0; i < fork_count; i++) {
		if (wait3 (&status, 0, 0) < 0) {
			if (errno != EINTR) {
				printf("wait3 error on %dth child\n", i);
				perror("wait3");
				return 1;
			}
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	int i;
	int ret;
	int count;
	char filename[NAME_SIZE];

	if (argc != 3) {
		printf ("Usage: %s filename filecount\n", argv[0]);
		exit (0);
	}

	prefix = argv[1];
	count = atoi (argv[2]);

	printf ("Start test the file:%s\n", argv[1]);
	for (i = 0; i < count; i++) {
		memset (filename, 0, NAME_SIZE);
		snprintf (filename, NAME_SIZE, "%s_%d\n", prefix, i);
		ret = create_file (filename);
		printf ("create file %s ret:%d\n", filename, ret);
	}	

	system ("echo 3 > /proc/sys/vm/drop_caches\n");
	sync ();

	for (i = 0; i < count; i++) {
		memset (filename, 0, NAME_SIZE);
		snprintf (filename, NAME_SIZE, "%s_%d\n", prefix, i);
		ret = remove_file (filename);
		printf ("remove file %s ret:%d\n", filename, ret);
	}	
	
	printf ("End test the file %s\n", argv[1]);

	return do_tasks();
}
