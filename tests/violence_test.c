#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <limits.h>

#ifndef O_DIRECT
#define O_DIRECT __O_DIRECT
#endif

#define NAME_SIZE  (512)
#define BUF_SIZE   (4096 * 3)  // for 3 lblock
#define ALIGN_SIZE (512)       // aligned to sector

#define dir_name_prefix   "test_dir_"
#define file_name_prefix  "test_inode_"

static int count;
static char *ourname;
static int dir_count;
static char * prefix;
static char * dirprefix;
static int delay = 1;
static int fork_count;
static int thread_count;

typedef void *(*__start_routine) (void *);
static int remove_file (const char *filename);

static void usage(const char * name)
{
	fprintf(stderr,
	"Usage: %s [options]\n"
	"    -p|--fprefix   	 set the prefix file name\n"
	"    -r|--rprefix   	 set the prefix dir name\n"
	"    -n|--fcount N       create N file in a directory to test\n"
	"    -s|--dcount N       create N directory to test\n"
	"    -d|--delay N 		 delay N s to work\n"
	"    -t|--thread N 		 do job in N threads\n"
	"    -f|--fork N      	 do job in N processes\n"
	"    -h|--help           show this message\n"
	, name);
}

static int read_task (const char *filename)
{
	int i;
	unsigned char *buf = NULL;
	int fd, ret = 0;

	posix_memalign ((void **)&buf, ALIGN_SIZE, BUF_SIZE);
	if (buf == NULL) {
		printf ("alloc memory failed.\n");
		return -1;
	}

	fd = open (filename, O_DIRECT, 0755);
	if (fd < 0) {
		printf ("open %s failed.\n", filename);
		free (buf);
		return -2;
	}

	// test for random data
	ret = read (fd, buf, BUF_SIZE);
	if (ret < BUF_SIZE) {
		printf ("read data failed.\n");
		return -3;
	}

	// test for 0x55
	ret = read (fd, buf, BUF_SIZE);
	if (ret < 0) {
		printf ("read data failed.\n");
		return -3;
	}
	for (i = 0; i < BUF_SIZE;i++) {
		if (buf[i] != 0x55) {
			printf ("read data failed for 0x55\n");
			break;
		}
	}

	// test for 0xaa
	ret = read (fd, buf, BUF_SIZE);
	if (ret < 0) {
		printf ("read data failed.\n");
		return -3;
	}
	for (i = 0; i < BUF_SIZE;i++) {
		if (buf[i] != 0xaa) {
			printf ("read data failed for 0xaa\n");
			break;
		}
	}

	// test for 0xa5
	ret = read (fd, buf, BUF_SIZE);
	if (ret < 0) {
		printf ("read data failed.\n");
		return -3;
	}
	for (i = 0; i < BUF_SIZE;i++) {
		if (buf[i] != 0xa5) {
			printf ("read data failed for 0xa5\n");
			break;
		}
	}

	// test for 0x5a
	ret = read (fd, buf, BUF_SIZE);
	if (ret < 0) {
		printf ("read data failed.\n");
		return -3;
	}
	for (i = 0; i < BUF_SIZE;i++) {
		if (buf[i] != 0x5a) {
			printf ("read data failed for 0x5a\n");
			break;
		}
	}

	free (buf);
	close (fd);

	return 0;
}

int get_file_size1 (const char *filename)
{
	FILE *fp = NULL;
	int filesize = 0;
	
	fp = fopen(filename , "r");
	if(NULL == fp){
		return 0;
	}
		
	fseek(fp, 0L, SEEK_END);
	filesize = ftell(fp);
	
	(void) fclose(fp);
	
	return filesize;
}

int get_file_size2(const char *filename)
{
	int fd = 0;
	int filesize = 0;
	struct stat stfile;
		
	fd = open(filename, O_RDONLY);
	if(fd < 0) {
		return 0;
	}

	fstat(fd, &stfile);
	filesize = (int) stfile.st_size;
	close (fd);
	
	return filesize ;
}

static int write_task (const char *filename)
{
	int i;
	unsigned char *buf = NULL;
	int fd, ret = 0;

	srand((unsigned) time(NULL ));
	posix_memalign ((void **)&buf, ALIGN_SIZE, BUF_SIZE);
	if (buf == NULL) {
		printf ("alloc memory failed.\n");
		return -1;
	}

	fd = open (filename, O_DIRECT | O_RDWR);
	if (fd < 0) {
		printf ("open %s failed.\n", filename);
		free (buf);
		return -2;
	}

	// test for random data
	for (i = 0; i < BUF_SIZE; i++) {
		buf[i] = rand() % BUF_SIZE;
	}

	ret = write (fd, buf, BUF_SIZE);
	if (ret < BUF_SIZE) {
		printf ("write size %d, but size is :%d\n", BUF_SIZE, ret);
		return -3;
	}
	printf ("write size %d\n", ret);

	// test for 0x55 data
	for (i = 0; i < BUF_SIZE; i++) {
		buf[i] = 0x55;
	}

	ret = write (fd, buf, BUF_SIZE);
	if (ret < BUF_SIZE) {
		printf ("write size %d, but size is :%d\n", BUF_SIZE, ret);
		return -3;
	}

	// test for 0xaa data
	for (i = 0; i < BUF_SIZE; i++) {
		buf[i] = 0xaa;
	}

	ret = write (fd, buf, BUF_SIZE);
	if (ret < BUF_SIZE) {
		printf ("write size %d, but size is :%d\n", BUF_SIZE, ret);
		return -3;
	}

	// test for 0xa5 data
	for (i = 0; i < BUF_SIZE; i++) {
		buf[i] = 0xa5;
	}

	ret = write (fd, buf, BUF_SIZE);
	if (ret < BUF_SIZE) {
		printf ("write size %d, but size is :%d\n", BUF_SIZE, ret);
		return -3;
	}

	// test for 0x5a data
	for (i = 0; i < BUF_SIZE; i++) {
		buf[i] = 0x5a;
	}

	ret = write (fd, buf, BUF_SIZE);
	if (ret < BUF_SIZE) {
		printf ("write size %d, but size is :%d\n", BUF_SIZE, ret);
		return -3;
	}

	free (buf);
	close(fd);

	return 0;
}

long do_units(void)
{
	int ret;
	long id;
	int filesize1 = 0;
	int filesize2 = 0;
	char filename[NAME_SIZE];	

	id = pthread_self();
	snprintf (filename, NAME_SIZE, "%s_%ld", prefix, id);
	if (access (filename, F_OK) == 0) {
		remove_file (filename);
	}

	filesize1 = get_file_size1 (filename);
	filesize2 = get_file_size2 (filename);
	if (filesize1 != filesize2) {
		printf ("some error happen\n");
	}

	ret = write_task (filename);
	if (ret < 0) {
		goto out;
	}

	ret  = read_task (filename);
out:

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

static int remove_dir (const char *dirname)
{
	if (access (dirname, F_OK) != 0) {
		printf ("no dir %s.\n", dirname);
		return -1;
	}

	if(rmdir (dirname) == -1){
		perror("rmdir error");
		return 0;
	}

	// check again
	if (access (dirname, F_OK) == 0) {
		printf ("remove %s failed.\n", dirname);
		return -1;
	}

	return 0;
}

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

static int make_dir (const char *dirname)
{
	if (access(dirname, F_OK) == 0) {
		printf ("%s are already created.\n", dirname);
		return 0;
	}

	if(mkdir (dirname, 777) == -1){
		perror("mkdir error");
		return 0;
	}

	// check again
	if (access(dirname, F_OK) != 0) {
		printf ("create %s failed.\n", dirname);
		return -1;
	}

	return 0;
}

static const struct option opts[] = {
	{ "fprefix"	, 1, NULL, 'p' },
	{ "rprefix"	, 1, NULL, 'r' },
	{ "fcount"	, 1, NULL, 'n' },
	{ "dcount"	, 1, NULL, 's' },
	{ "delay"	, 1, NULL, 'd' },
	{ "thread"	, 1, NULL, 't' },
	{ "fork"	, 1, NULL, 'f' },
	{ "help"	, 0, NULL, 'h' },
};

int main(int argc, char *argv[])
{
	int i;
	int c;
	int j;
	int ret;
	int pagesize;
	int opt_index = 0;
	char dirname[NAME_SIZE];
	char oldpath[NAME_SIZE];
	char filename[NAME_SIZE];

	ourname = argv[0];
	pagesize = getpagesize();
	while ((c = getopt_long(argc, argv,
				"p:n:t:f:d:s:r:h",
				opts, &opt_index)) != -1)
		{
		switch (c) {
		case 'p':
			prefix = optarg;
			break;
		case 'r':
			dirprefix = optarg;
			break;
		case 'n':
			count = strtol(optarg, NULL, 10);
			break;
		case 's':
			dir_count = strtol(optarg, NULL, 10);
			break;
		case 'd':
			delay = strtol(optarg, NULL, 10);
			break;
		case 't':
			thread_count = strtol(optarg, NULL, 10);
			break;
		case 'f':
			fork_count = strtol(optarg, NULL, 10);
			break;
		case 'h':
			usage(ourname);
			break;

		default:
			usage(ourname);
		}
	}

	if (prefix == NULL) {
		fprintf(stderr,
			"%s: must set the prefix name for test\n",
			argv[0]);
		exit (1);
	}

	if (dir_count == 0) {
		fprintf(stderr,
			"%s: must set the dir count for test\n",
			argv[0]);
		exit (1);
	}

	if (count == 0) {
		fprintf(stderr,
			"%s: must set the file count in per dir for test\n",
			argv[0]);
		exit (1);
	}

	if (dirprefix == NULL) {
		dirprefix = dir_name_prefix;
	}

	if (prefix == NULL) {
		prefix = file_name_prefix;
	}

	getcwd(oldpath, NAME_SIZE);
	printf ("Start test the file\n");

	system ("echo 2 > /proc/sys/vm/drop_caches\n");
	for (j = 0; j < dir_count; j++) {
		memset (dirname, 0, NAME_SIZE);
		snprintf (dirname, NAME_SIZE, "%s_%d\n", dirprefix, j);

		if (make_dir (dirname) < 0) {
			printf ("mkdir failed\n");
			continue;  // next one
		}

		if (chdir(dirname) < 0) {
			printf ("chdir failed\n");
			continue;  // next one
		}

		for (i = 0; i < count; i++) {
			memset (filename, 0, NAME_SIZE);
			snprintf (filename, NAME_SIZE, "%s_%d\n", prefix, i);
			ret = create_file (filename);
			printf ("create file %s ret:%d\n", filename, ret);
		}

		chdir(oldpath); 
	}

	sync ();
	system ("echo 2 > /proc/sys/vm/drop_caches\n");

	for (j = 0; j < dir_count; j++) {
		memset (dirname, 0, NAME_SIZE);
		snprintf (dirname, NAME_SIZE, "%s_%d\n", dirprefix, j);

		if(chdir(dirname) < 0) {
			printf ("chdir failed\n");
			continue;  // next one
		}

		for (i = 0; i < count; i++) {
			memset (filename, 0, NAME_SIZE);
			snprintf (filename, NAME_SIZE, "%s_%d\n", prefix, i);
			ret = remove_file (filename);
			printf ("remove file %s ret:%d\n", filename, ret);
		}	

		chdir(oldpath); 

		if (remove_dir (dirname) < 0) {
			printf ("rmdir failed\n");
			continue;  // next one
		}
	}
	
	sync ();
	system ("echo 2 > /proc/sys/vm/drop_caches\n");

	sleep (delay);
	(void) pagesize;
	printf ("End test the file\n");

	return do_tasks();
}
