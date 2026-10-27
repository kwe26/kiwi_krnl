#ifndef _DIRENT_H
#define _DIRENT_H
#include <kiwi/abi.h>
struct dirent {
    uint32_t d_ino;
    uint32_t d_type;
    uint32_t d_size;
    uint32_t d_csize;
    char     d_name[64];
};
typedef struct _kiwi_dir DIR;
DIR           *opendir(const char *path);
struct dirent *readdir(DIR *d);
int            closedir(DIR *d);
#endif
