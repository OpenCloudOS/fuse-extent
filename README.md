# fuse-extent

fuse-extent项目致力于扩展fuse的功能，增强fuse的性能。目前主要包括两个子项目：
1. fuse-crash-recovery
 该子项目主要是构建一个基于fuse的用户态文件系统的crash自动恢复框架。
2. fuse-based-ebpf
 该子项目主要是基于ebpf来提升fuse的性能。
##fuse-crash-recovery

### 原理

本项目基于libfuse(https://github.com/libfuse/libfuse.git)构建。整个方案实现包含两部分，一部分在Linux
内核的fuse部分，主要实现在crash恢复阶段将in-flighting的IO请求重新放回fuse的Pending队列中，以待恢复后的
用户态fuse文件系统服务重新获取此IO请求；另一部分基于libfuse构建（是否使用libfuse并没有强依赖)，在libfuse
的passthrough_ll样例中展示了用户态的实现方式。

Linux内核部分见patch：
https://github.com/OpenCloudOS/OpenCloudOS-Kernel/commit/e1c207b3e7cdfd98ce1120a38c979d748e95f958

### 进展

目前前置要求用户态fuse服务预先创建文件。在文件的读写操作时出现fuse服务进程crash，可以实现用户无感知
的自动恢复。即目前的实现支持只读fuse文件系统的恢复，但是同时还支持对文件的写操作。
下一步计划支持文件的创建/删除等更多通用操作。

### 用户态测试程序

测试方式：
```
$gcc -o testfile testfile.c -D_GNU_SOURCE
$./example/passthrough_ll -o debug -s  /mnt3
$./testfile
$ ps aux | grep pass
$root       34889  0.0  0.0   8848   864 pts/2    S+   13:10   0:00 ./example/passthrough_ll -o debug -s /mnt3
$root       34896  0.0  0.0   9880   128 pts/2    S+   13:10   0:00 ./example/passthrough_ll -o debug -s /mnt3
$root       34913  0.0  0.0  12112  1060 pts/1    S+   13:10   0:00 grep --color=auto pass
//kill child process
$ kill 34896
```
read test
```
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h> 
#include <string.h>

void main(void)
{
       int fd, size = 0, ret=0;
       unsigned char *buf;
       posix_memalign((void **)&buf, 512, 4096);
       fd = open("/mnt3/test1.file", O_DIRECT,0755);
       while ( (ret = read(fd,buf,4096)) > 0) {
               sleep(1);
               size += ret;
               printf(" read +%d, t=%d \n", ret, size);
       }
}
```
write test
```
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

void main(void)
{
        int fd, size = 0, ret=0, i;
        unsigned char *buf;
        posix_memalign((void **)&buf, 512, 4096);
        for (i = 0; i < 4096; i++)
                buf[i] = i;
        fd = open("/mnt3/test1.file", O_DIRECT | O_RDWR);
        while ( (ret = write(fd,buf,4096)) > 0) {
                sleep(1);
                size += ret;
                printf(" write +%d, t=%d \n", ret, size);
        }
}
```
## fuse-based-ebpf

###原理

fuse-based-ebpf的优化主要是针对部分操作，如高频的lookup，通过ebpf在内核部分生成cache，在用户执行
lookup时无需再与用户态文件系统服务进程交互，从而提升fuse文件系统的性能。

###组件
组件包括一个libextfuse库（包括epbf程序），内核patch: https://github.com/OpenCloudOS/OpenCloudOS-Kernel/commit
/eab7730c17c6ed5d61efdf01e7213674e37d863f.
