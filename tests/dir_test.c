#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
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

int main(int argc, char *argv[])
{
	int i;
	int j;
	int ret;
	int count;
	char * prefix;
	char dirname[NAME_SIZE];
	char oldpath[NAME_SIZE];
	char filename[NAME_SIZE];

	if (argc != 3) {
		printf ("Usage: %s filename filecount\n", argv[0]);
		exit (0);
	}

	prefix = argv[1];
	count = atoi (argv[2]);

	getcwd(oldpath, NAME_SIZE);
	printf ("Start test the file:%s\n", argv[1]);

	for (j = 0; j < count / 2; j++) {
		memset (dirname, 0, NAME_SIZE);
		snprintf (dirname, NAME_SIZE, "%s_%d\n", dir_name_prefix, j);

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

	for (j = 0; j < count / 2; j++) {
		memset (dirname, 0, NAME_SIZE);
		snprintf (dirname, NAME_SIZE, "%s_%d\n", dir_name_prefix, j);

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
	
	printf ("End test the file %s\n", argv[1]);

	return 0;
}
