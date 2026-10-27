#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H
#include <sys/types.h>
#include <kiwi/abi.h>
#define MAP_FAILED ((void *)-1)
/* Kiwi supports anonymous memory and device mappings (/dev/fb0). */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int   munmap(void *addr, size_t len);
#endif
