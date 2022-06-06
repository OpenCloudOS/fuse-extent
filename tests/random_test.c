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

#ifndef O_DIRECT
#define O_DIRECT __O_DIRECT
#endif

#define BUF_SIZE   (4096 * 3)  // for 3 lblock
#define ALIGN_SIZE (512)       // aligned to sector

static int read_child (const char *filename)
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

static int write_child (const char *filename)
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

int main(int argc, char *argv[])
{
	int ret;

	if (argc != 2) {
		printf ("Usage: %s filename\n", argv[0]);
		exit (0);
	}

	printf ("Start test the file:%s\n", argv[1]);
	ret = write_child (argv[1]);
	if (ret < 0) {
		goto out;
	}

	ret  = read_child (argv[1]);
out:
	printf ("End test the file %s, ret is %d\n", argv[1], ret);

	return 0;
}
