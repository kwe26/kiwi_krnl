/*
 * Kiwi kernel <-> userland ABI.  Included by both the kernel and libc.
 *
 * System calls: int $0x80, eax = number, args in ebx ecx edx esi edi,
 * result in eax (negative errno on failure).
 */
#ifndef _KIWI_ABI_H
#define _KIWI_ABI_H

#include <stdint.h>

#define KIWI_NAME     "Kiwi"
#define KIWI_RELEASE  "0.1.0"

/* ---- system call numbers ---- */
#define SYS_exit      1
#define SYS_fork      2
#define SYS_read      3
#define SYS_write     4
#define SYS_open      5
#define SYS_close     6
#define SYS_waitpid   7
#define SYS_execve    8
#define SYS_getpid    9
#define SYS_sbrk      10
#define SYS_lseek     11
#define SYS_stat      12
#define SYS_fstat     13
#define SYS_readdir   14
#define SYS_mkdir     15
#define SYS_unlink    16
#define SYS_chdir     17
#define SYS_getcwd    18
#define SYS_uptime    19
#define SYS_ioctl     20
#define SYS_kzstat    21
#define SYS_kzctl     22
#define SYS_sleep     23
#define SYS_uname     24
#define SYS_rename    25
#define SYS_dup2      26
#define SYS_pipe      27
#define SYS_ps        28
#define SYS_kill      29
#define SYS_time      30
#define SYS_getppid   31
#define SYS_spawn     32
#define SYS_setpgid   33
#define SYS_dup       34
#define SYS_poweroff  35
#define SYS_ftruncate 36
#define SYS_mmap      37
#define SYS_munmap    38
#define SYS_portio    39
#define SYS_iopl      40
#define SYS_kinfo     41
#define SYS_peek      42
#define SYS_MAX       43

/* ---- errno ---- */
#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define E2BIG        7
#define ENOEXEC      8
#define EBADF        9
#define ECHILD      10
#define EAGAIN      11
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EBUSY       16
#define EEXIST      17
#define EXDEV       18
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define ENFILE      23
#define EMFILE      24
#define ENOTTY      25
#define EFBIG       27
#define ENOSPC      28
#define ESPIPE      29
#define EPIPE       32
#define EDOM        33
#define ERANGE      34
#define ENAMETOOLONG 36
#define ENODEV      19
#define ENOSYS      38
#define ENOTEMPTY   39

/* ---- open() flags ---- */
#define O_RDONLY     0x0000
#define O_WRONLY     0x0001
#define O_RDWR       0x0002
#define O_ACCMODE    0x0003
#define O_CREAT      0x0040
#define O_EXCL       0x0080
#define O_TRUNC      0x0200
#define O_APPEND     0x0400
#define O_DIRECTORY  0x10000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* ---- file types ---- */
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFREG  0100000
#define S_IFIFO  0010000
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)

struct kiwi_stat {
    uint32_t ino;
    uint32_t mode;
    uint32_t size;        /* logical size */
    uint32_t csize;       /* bytes actually held in RAM (compressed) */
    uint32_t nlink;
    uint32_t mtime;
};

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_REG     8

struct kiwi_dirent {
    uint32_t d_ino;
    uint32_t d_type;
    uint32_t d_size;
    uint32_t d_csize;
    char     d_name[64];
};

/* ---- wait ---- */
#define WNOHANG 1
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WTERMSIG(s)    ((s) & 0x7f)
#define WIFEXITED(s)   (WTERMSIG(s) == 0)
#define WIFSIGNALED(s) (WTERMSIG(s) != 0)

#define SIGINT  2
#define SIGKILL 9
#define SIGTERM 15

/* ---- tty ioctl ---- */
#define TTY_SETRAW   1   /* arg: 1 = raw (no echo, byte at a time), 0 = cooked */
#define TTY_GETRAW   2
#define TTY_SETFG    3   /* arg: foreground process group (gets ^C) */
#define TTY_GETSIZE  4   /* returns (rows << 16) | cols */
#define TTY_ISATTY   5

/* ---- memory compression (zram) ---- */
#define KZ_SET_BUDGET     1  /* arg: max resident user pages (0 = auto) */
#define KZ_GET_BUDGET     2
#define KZ_COMPRESS_ALL   3  /* compress every idle page of other processes */
#define KZ_SET_PROACTIVE  4  /* arg: 0 = off, N = compress after N idle scans */

struct kiwi_kzstat {
    uint32_t page_size;
    uint32_t total_frames;     /* physical RAM in pages */
    uint32_t free_frames;
    uint32_t user_resident;    /* user pages currently in RAM */
    uint32_t budget;           /* max resident user pages */
    uint32_t proactive;        /* idle-scan threshold, 0 = off */

    uint32_t z_pages;          /* logical pages stored compressed */
    uint32_t z_slots;          /* unique compressed objects */
    uint32_t z_same;           /* same-filled pages (zero pages etc.) */
    uint32_t z_lz;             /* pages stored LZ-compressed */
    uint32_t z_lzh;            /* pages stored LZ+Huffman (idle-time, heavy) */
    uint32_t z_raw;            /* incompressible pages stored as-is */
    uint32_t z_bytes;          /* bytes used by the compressed pool */
    uint32_t z_dedup;          /* page-outs resolved by deduplication */
    uint32_t z_pageouts;       /* total pages compressed */
    uint32_t z_pageins;        /* total pages decompressed on fault */

    uint32_t fs_files;
    uint32_t fs_bytes;         /* logical bytes in ramfs */
    uint32_t fs_zbytes;        /* compressed bytes held by ramfs */

    uint32_t heap_total;       /* kernel heap */
    uint32_t heap_used;
};

struct kiwi_procinfo {
    int32_t  pid, ppid, pgid, state;
    uint32_t resident;         /* pages in RAM */
    uint32_t compressed;       /* pages in zram */
    uint32_t ticks;
    char     name[32];
};

#define PSTATE_RUNNING 1
#define PSTATE_BLOCKED 2
#define PSTATE_ZOMBIE  3

struct kiwi_utsname {
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[64];
    char machine[32];
};

/* posix_spawn-like process creation (no address space copy) */
struct kiwi_spawn {
    const char  *path;
    char *const *argv;
    int32_t      fd_in, fd_out, fd_err;  /* -1: inherit */
    int32_t      pgid;                   /* 0: inherit, -1: new group */
};

/* ---- memory mapping ---- */
#define PROT_NONE   0
#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4
#define MAP_SHARED  0x01
#define MAP_PRIVATE 0x02
#define MAP_ANON    0x20
#define MAP_ANONYMOUS MAP_ANON

struct kiwi_mmap {
    uint32_t addr, len;
    int32_t  prot, flags, fd;
    uint32_t off;
};

#define MMAP_BASE  0x80000000u      /* mmap() area grows up from here */
#define MMAP_END   0xBF000000u

/* ---- port I/O (SYS_portio) ---- */
#define PIO_INB  0
#define PIO_INW  1
#define PIO_INL  2
#define PIO_OUTB 3
#define PIO_OUTW 4
#define PIO_OUTL 5

#include "fb.h"
#include "input.h"
#include "kinfo.h"

#endif
