#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#include <sys/types.h>
#include <kiwi/abi.h>
struct stat {
    ino_t   st_ino;
    mode_t  st_mode;
    off_t   st_size;
    off_t   st_csize;   /* Kiwi: bytes held in RAM after compression */
    nlink_t st_nlink;
    time_t  st_mtime;
};
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int mkdir(const char *path, mode_t mode);
#endif
