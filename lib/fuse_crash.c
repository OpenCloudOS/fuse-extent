#include <fuse_crash.h>

int send_fd(int fd, void *sendbuf, size_t size, int sendfd)
{
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));

    struct iovec io_vector;
    io_vector.iov_base = sendbuf;
    io_vector.iov_len = size;
    msg.msg_iov = &io_vector;
    msg.msg_iovlen = 1;

    if (sendfd)
    {
        union
        {
            struct cmsghdr cm;
            char control[CMSG_SPACE(sizeof(int))];
        } control_un;
        msg.msg_control = control_un.control;
        msg.msg_controllen = sizeof(control_un.control);

        struct cmsghdr *cmptr = CMSG_FIRSTHDR(&msg);
        cmptr->cmsg_level = SOL_SOCKET;
        cmptr->cmsg_type = SCM_RIGHTS;
        cmptr->cmsg_len = CMSG_LEN(sizeof(int));
        *((int *)CMSG_DATA(cmptr)) = sendfd;
    }

    return sendmsg(fd, &msg, 0);
}

int recv_fd(int fd, void *recvbuf, size_t size, int *recvfd)
{
    int res;
    struct msghdr msg;
    struct cmsghdr *msptr = NULL;
    memset(&msg, 0, sizeof(struct msghdr));
    *recvfd=0;

    struct iovec io_vector;
    io_vector.iov_base = recvbuf;
    io_vector.iov_len = size;
    msg.msg_iov = &io_vector;
    msg.msg_iovlen = 1;

    union
    {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    } control_un;
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    res = recvmsg(fd, &msg, 0);
    if (res < 0)
        return res;

    if (strstr(recvbuf, "close") == NULL && strstr(recvbuf, "closedir") == NULL)
    {
        msptr = CMSG_FIRSTHDR(&msg);
        if (msptr == NULL ||
            msptr->cmsg_level != SOL_SOCKET ||
            msptr->cmsg_type != SCM_RIGHTS ||
            msptr->cmsg_len != CMSG_LEN(sizeof(int)))
            return -1;

        *recvfd = *((int *)CMSG_DATA(msptr));
    }
    return res;
}