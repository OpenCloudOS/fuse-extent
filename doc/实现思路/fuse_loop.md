# 总览
`fuse_loop.c` 实现了在文件系统挂载成功后，不断从 /dev/fuse 设备文件中读取请求随后处理请求的过程。

## 多线程可能的实现思路
1. 一种是少数IO线程不断读取请求，存入任务队列，然后多个工作线程从任务队列中取出请求并处理，这是常见的服务线程模型，这里不采用。
2. 另外一种是创建多个线程，每个单一的线程都重复读取请求，处理请求，然后响应的过程，我们这里就采用这种实现方式。另外，某一个线程因为故障退出会导致所有其他线程退出。
### libfuse 中对于多线程的实现
libfuse的实现，每当新的请求到来时，检查是否有空闲线程，通过 numavail 记录了当前可以使用的空闲线程数量，如果这个值为零，那么重新创建一个新的线程处理。
通过 numworker 记录当前创建的线程总数。同时，当请求处理完时，检查 numavail 是否大于 max_idles，max_idles 是处理过程中允许的最大空闲线程的数量，如果大于这个值，那么就释放多余的线程。
### 加快处理效率
如果设置了 clone_fd=1，那么对于每个线程，都会进行系统调用 ioctl(FUSE_CLONE_FD) 拷贝原有的 fuse_conn，产生一个新的 fuse_dev，但是所有的 fuse_dev 共享同一个 fuse_conn。每个线程就读取它们所对应的 fuse_dev，这样可以加快处理效率。fuse 内核中，请求的输入队列记录在 fuse_conn，每个 fuse_dev 都有它们对应的处理队列。

## 导致循环退出的原因
这里将 `fuse_single_session_loop()` 以及 `fuse_multi_session_loop()` 统称为 `fuse_session_loop`
1. `fuse_session_loop` 过程中调用 `fusermount -u` 解挂对应的文件系统。那么随后对 /dev/fuse 的读取操作将会返回 errno=ENODEV，文件系统解除挂载。
2. `fuse_session_loop` 过程中调用 `umount` 解挂对应的文件系统。那么随后对 /dev/fuse 的读取操作将会返回 errno=ENODEV，文件系统解除挂载。
3. `fuse_session_loop` 过程中 `echo 1>> /sys/fs/fuse/connections/NN/abort` 解挂对应的文件系统。那么随后对 /dev/fuse 的读取操作将会返回 errno=ENODEV。在此之后内核所有输入队列以及处理队列的请求都会被抛弃，在这之后还需要进一步对文件系统通过 `fusermount -u` 或者 `umount` 解除挂载，通过调用 `fuse_session_unmount()` 函数并不会解挂文件系统，它只会因为 POLLERR 而退出。如果没有手动解除挂载，那么对应的文件系统并未解除挂载，访问文件系统将提示 `Transport endpoint is not connected`。
4. 如果 `fuse_session_loop` 过程中意外中止（中止后进程直接退出，没有调用 `fuse_session_unmount()`）。此时，文件系统并未解除挂载，访问文件系统将提示 `Transport endpoint is not connected`，这是一种可能的 CRASH_RECOVERT 状态

## 守护进程的创建
进程组：进程组简单来说就是一组进程，他们有相同的进程组 ID，可以向一个进程组里的所有进程发送信号。一般使用管道可以创建一个进程组。进程组号 PRGP 为进程组中第一个进程的 PID，它也是这个进程组的组长 (process group leader)。
会话：一般情况下，一个 shell 窗口会打开一个会话，在这个 shell 里执行的所有命令都属于这个会话。每个会话有一个组长 (session leader)，如果你用的是bash，那么组长就很可能是这个进程，而会话 ID 也就是这个进程的 ID。

通过调用 setsid() 函数（还有一个setsid命令）来创建一个新的会话，调用这个函数的程序不能是进程组长(process group leader)，如果是则会出错。如果调用进程是一个进程组长，可以先调用 fork() 函数，退出父进程，在子进程中调用 setsid()。因此，我们对守护进程的创建如下：
```c
int pid=fork();
if(pid<0)
    //error
if(pid==0){
    setsid();
}else{
    exit(0)
}
```