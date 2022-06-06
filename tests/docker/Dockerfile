FROM centos:8
MAINTAINER kai

RUN yum install git xfsprogs patch make gcc -y
RUN yum install -y e4fsprogs
RUN yum install -y numactl-devel
RUN yum clean all
RUN git clone  https://git.kernel.org/pub/scm/linux/kernel/git/wfg/vm-scalability.git
RUN cd vm-scalability && make

ADD jobs/ /vm-scalability
