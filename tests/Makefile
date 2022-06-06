all: test 
LD ?= ld
CC ?= gcc
STRIP ?= strip

SYSROOT := $(shell pwd)/testroot
DOCKER_ROOT := $(shell pwd)/docker/jobs

LDFLAGS += -lc -L. -L./lib
CFLAGS += -O2 -Werror -Wall -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 \
		  -Wno-error=deprecated-declarations -Wno-deprecated-declarations \
		  -Wno-error=pointer-to-int-cast

objs := sample_test random_test file_test mmap_test \
		self_test filesize_test dir_test  \
		violence_test

test: $(objs)

sample_test: sample_test.o
	$(CC) $(LDFLAGS) $< -o $@
	$(STRIP) $@

sample_test.o: sample_test.c
	$(CC) $(CFLAGS) -c $< -o $@

random_test: random_test.o
	$(CC) $(LDFLAGS) $< -o $@
	$(STRIP) $@

random_test.o: random_test.c
	$(CC) $(CFLAGS) -c $< -o $@

file_test: file_test.o
	$(CC) -lpthread $(LDFLAGS) $< -o $@
	$(STRIP) $@

file_test.o: file_test.c
	$(CC) $(CFLAGS) -c $< -o $@

mmap_test: mmap_test.o
	$(CC) $(LDFLAGS) $< -o $@
	$(STRIP) $@

mmap_test.o: mmap_test.c
	$(CC) $(CFLAGS) -c $< -o $@

self_test: self_test.o
	$(CC) $(LDFLAGS) $< -o $@
	$(STRIP) $@

self_test.o: self_test.c
	$(CC) $(CFLAGS) -c $< -o $@

filesize_test: filesize_test.o
	$(CC) $(LDFLAGS) $< -o $@
	$(STRIP) $@

filesize_test.o: filesize_test.c
	$(CC) $(CFLAGS) -c $< -o $@

dir_test: dir_test.o
	$(CC) $(LDFLAGS) $< -o $@
	$(STRIP) $@

dir_test.o: dir_test.c
	$(CC) $(CFLAGS) -c $< -o $@

violence_test: violence_test.o
	$(CC) $(LDFLAGS) -lpthread $< -o $@
	$(STRIP) $@

violence_test.o: violence_test.c
	$(CC) $(CFLAGS) -c $< -o $@

install:
	@mkdir -p $(SYSROOT)
	@mkdir -p $(SYSROOT)/bin
	cp $(objs) $(SYSROOT)/bin
	cp $(objs) $(DOCKER_ROOT)/bin

clean:
	rm -rf *.o
	rm -rf $(objs)
	rm -rf $(SYSROOT)
	rm -rf $(DOCKER_ROOT)/bin/*

.PHONY: test clean
