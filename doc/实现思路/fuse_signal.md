# 信号类型
Linux系统共定义了64种信号，分为两大类：可靠信号与不可靠信号，前32种信号为不可靠信号，后32种为可靠信号。
## 概念
- 不可靠信号： 也称为非实时信号，不支持排队，信号可能会丢失, 比如发送多次相同的信号, 进程只能收到一次. 信号值取值区间为1~31；
- 可靠信号： 也称为实时信号，支持排队, 信号不会丢失, 发多少次, 就可以收到多少次. 信号值取值区间为32~64

## 待处理的信号
|取值|信号名|解释|产生方式|
|--|--|--|--|
|1|SIGHUP|挂起|执行当前进程的终端挂起或者控制进程终止，之后进程将会自动成为初始化进程（PID=1）的子进程|
|2|SIGINT|中断|用户按下 Ctrl+C 组合键时产生该信号，发送给终端中当前运行的进程，默认动作为终止进程|
|3|SIGQUIT|退出|用户按下 Ctrl+/ 组合键时产生该信号，发送给终端中当前运行的进程，默认动作为终止进程并产生 core 文件|
|9|SIGKILL|kill|无条件终止进程，本信号不能被忽略、处理和阻塞。默认动作为终止进程，它向系统管理员提供了一种可以杀死任何进程的方法。|
|15|SIGTERM|终止|通过 shell 执行 kill 命令，如果不指定任何参数，则默认产生这个信号，默认动作为终止进程。类似于 SIGKILL，不过它可以被阻塞和处理|

# 信号产生
信号来源分为硬件类和软件类：
## 硬件方式
- 用户输入：比如在终端上按下组合键 Ctrl+C，产生SIGINT信号；
- 硬件异常：CPU检测到内存非法访问等异常，通知内核生成相应信号，并发送给发生事件的进程；
## 软件方式
通过系统调用，发送signal信号：kill()，raise()，sigqueue()，alarm()，setitimer()，abort()


# 实现方式
`fuse_signal.c` 文件主要实现了对接收到信号的处理方式。默认情况下，在接收到 SIGHUP/SIGINT/SIGQUIT/SIGTERM 会终止进程，我们对这些信号的处理函数进行了重新设置。工作进程在接收到这些信号时，它们会设置当前 fuse_session 的 exited 字段为1，使得它们能够平稳地退出 `fuse_session_loop`，完成后续的清理工作；故障恢复模式下，会额外有一个故障恢复进程，需要设置故障恢复进程忽略这些信号。

## sigaction
sigaction 结构体中有三个字段，分别是 sa_handler, sa_mask, sa_flags.
### sa_handler
sa_handler 为信号的处理函数，SIG_DFL 表示默认的动作，SIG_IGN 表示忽略信号
### sa_mask
通过设置这个字段，可以在调用信号处理程序时就能阻塞某些信号。注意仅仅是在信号处理程序正在执行时才能阻塞某些信号，如果信号处理程序执行完了，那么依然能接收到这些信号。
### sa_flags
在我们的信号处理设置中，可能需要用到的标志为 SA_RESTART。默认情况下，在进行 `sleep,wait,read,write` 等系统调用期间，如果进程收到了信号，那么会中断当前系统调用，并设置 errno 为 EINTR，进程需要重新发起系统调用。通过设置 SA_RESTART，在这些系统调用期间如果被信号打断，那么会自动重启系统调用。

## pthread_sigmask
进程中的所有线程共享一个信号集，但是每个线程均有自己的信号屏蔽集（信号掩码），可以使用 pthread_sigmask 函数来屏蔽某个线程对某些信号的响应处理，仅留下需要处理该信号的线程来处理指定的信号。在多线程模型中，所有其他衍生的线程通过 pthread_sigmask 屏蔽 SIGHUP, SIGINT, SIGQUIT, SIGTERM 这些信号，只通过主线程来处理这些信号。主线程在收到信号之后，设置se->exited=1 之后，通过 pthread_cancel 来终止其他衍生的线程。另外，在其他衍生的线程中需要通过 pthread_setcancelstate 来设置取消点。

A signal may be generated (and thus pending) for a process as a whole (e.g., when sent using kill(2)) or for a specific thread (e.g., certain signals, such as SIGSEGV and SIGFPE, generated as a consequence of executing a specific machine-language instruction are thread directed, as are signals targeted at a specific thread using pthread_kill(3)). A process-directed signal may be delivered to any one of the threads that does not currently have the signal blocked. If more than one of the threads has the signal unblocked, then the kernel chooses an arbitrary thread to which to deliver the signal.

# 其他
在利用 vscode debug 时，在终端键入 Ctrl+C 是不能向执行中的进程发送 SIGINT 信号的。

