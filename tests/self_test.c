#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>

#define MEMORY_TEST_SIZE (10*1024*1024)

void usage(char *name)
{
    fprintf(stderr, "usage: %s COUNT script bigfile\n", name);
    exit(1);
}

int do_task (char * script)
{
    char *args[1] = {0};

    return execv(script, args);
}

int do_memory (char *bigfile)
{
    int fd;
    char *buffer;

    fd = open (bigfile, O_RDONLY);
    if (fd < 0) {
        printf ("%s not found\n", bigfile);
        exit (0);
    }

    buffer = (char *) malloc (MEMORY_TEST_SIZE);
    if (buffer == NULL) {
        printf ("can't malloc\n");
        close (fd);
        exit (0);
    }
    (void) read (fd, buffer, MEMORY_TEST_SIZE);
    buffer [MEMORY_TEST_SIZE] = '\0';

    free (buffer);

    close (fd);
    return 0;
}

int main (int argc, char *argv[])
{
    int i;
    int pid;
    int status;
    int nr_task;

    if (argc != 4) {
        usage(argv[0]);
        exit(0);
    }

    nr_task = atoi(argv[1]);
    for (i = 0; i < nr_task/2; i++) {
        if ((pid = fork()) == 0) {
            return do_task(argv[2]);
        } else if (pid < 0) {
            fprintf(stderr, "failed to fork\n");
        }
    }

    while (1) {
        for (i = 0; i < nr_task/2; i++) {
            if ((pid = fork()) == 0) {
                return do_memory(argv[3]);
            } else if (pid < 0) {
                fprintf(stderr, "failed to fork\n");
            }
        }

        for (i = 0; i < nr_task/2; i++) {
            (void) wait3(&status, 0, 0);
        }
    }

    for (i = 0; i < nr_task/2; i++) {
        (void) wait3(&status, 0, 0);
    }
    printf ("fork exit\n");

    return 0;
}
