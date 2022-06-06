#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <string.h>
#include <sys/wait.h>

#ifndef O_DIRECT
#define O_DIRECT __O_DIRECT
#endif

static int read_child (const char *filename)
{
	unsigned char *buf = NULL;
	int fd, size = 0, ret = 0;

	posix_memalign ((void **)&buf, 512, 4096);
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

	while ((ret = read (fd, buf, 4096)) > 0) {
		size += ret;
		printf(" read +%d, t=%d \n", ret, size);
		sleep(1);
	}
	free (buf);
	close (fd);

	return 0;
}

static int write_child (const char *filename)
{
	int i;
	unsigned char *buf = NULL;
	int fd, size = 0, ret = 0;

	posix_memalign ((void **)&buf, 512, 4096);
	if (buf == NULL) {
		printf ("alloc memory failed.\n");
		return -1;
	}

	for (i = 0; i < 4096; i++) {
		buf[i] = i;
	}

	fd = open (filename, O_DIRECT | O_RDWR);
	if (fd < 0) {
		printf ("open %s failed.\n", filename);
		free (buf);
		return -2;
	}

	while ((ret = write (fd, buf, 4096)) > 0) {
		size += ret;
		printf(" write +%d, t=%d \n", ret, size);
		sleep(1);
	}
	free (buf);
	close(fd);

	return 0;
}

int main(int argc, char *argv[])
{
	int pid_r;
	int pid_w;

	if (argc != 2) {
		printf ("Usage: %s filename\n", argv[0]);
		exit (0);
	}

	printf ("Start test the file:%s\n", argv[1]);
	pid_w = fork ();
	if (pid_w == 0) {
		write_child (argv[1]);
		exit (0);
	}
	pid_r = fork ();
	if (pid_r == 0) {
		read_child (argv[1]);
		exit (0);
	}

	(void) waitpid (pid_w, NULL, 0);
	(void) waitpid (pid_r, NULL, 0);
	printf ("End test the file %s\n", argv[1]);

	return 0;
}
