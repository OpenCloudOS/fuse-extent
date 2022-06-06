#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFFER_SIZE 4086

void usage(char *name)
{
	printf ("Usage: %s filename\n", name);
    exit(1);
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

int main(int argc, char *argv[])
{
	int filesize1 = 0;
	int filesize2 = 0;
	char name[BUFFER_SIZE];

    if (argc != 2) {
        usage(argv[0]);
    }

	memset(name, 0, sizeof(name));
	sprintf (name, "%s", argv[1]);
	
	filesize1 = get_file_size1 (name);
	filesize2 = get_file_size2 (name);

	if (filesize1 != filesize2) {
		printf ("some error happen\n");
	}

	return 0;
}

