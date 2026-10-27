/* System call wrappers: unistd.h, fcntl.h, sys/ headers, dirent.h, kiwi/kiwi.h */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <kiwi/kiwi.h>

int errno;

long kiwi_syscall(long n, long a, long b, long c, long d, long e)
{
    long r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory");
    return r;
}

static long sc(long n, long a, long b, long c)
{
    long r = kiwi_syscall(n, a, b, c, 0, 0);
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

void _exit(int status)
{
    kiwi_syscall(SYS_exit, status, 0, 0, 0, 0);
    for (;;)
        ;
}

ssize_t read(int fd, void *buf, size_t n)       { return sc(SYS_read, fd, (long)buf, (long)n); }
ssize_t write(int fd, const void *buf, size_t n) { return sc(SYS_write, fd, (long)buf, (long)n); }
int     close(int fd)                           { return (int)sc(SYS_close, fd, 0, 0); }
off_t   lseek(int fd, off_t off, int whence)    { return sc(SYS_lseek, fd, off, whence); }
int     dup(int fd)                             { return (int)sc(SYS_dup, fd, 0, 0); }
int     dup2(int a, int b)                      { return (int)sc(SYS_dup2, a, b, 0); }
int     pipe(int fds[2])                        { return (int)sc(SYS_pipe, (long)fds, 0, 0); }
pid_t   fork(void)                              { return (pid_t)sc(SYS_fork, 0, 0, 0); }
pid_t   getpid(void)                            { return (pid_t)sc(SYS_getpid, 0, 0, 0); }
pid_t   getppid(void)                           { return (pid_t)sc(SYS_getppid, 0, 0, 0); }
int     setpgid(pid_t p, pid_t g)               { return (int)sc(SYS_setpgid, p, g, 0); }
int     chdir(const char *p)                    { return (int)sc(SYS_chdir, (long)p, 0, 0); }
int     unlink(const char *p)                   { return (int)sc(SYS_unlink, (long)p, 0, 0); }
int     rmdir(const char *p)                    { return (int)sc(SYS_unlink, (long)p, 0, 0); }
int     mkdir(const char *p, mode_t m)          { (void)m; return (int)sc(SYS_mkdir, (long)p, 0, 0); }
int     rename(const char *a, const char *b)    { return (int)sc(SYS_rename, (long)a, (long)b, 0); }
int     ftruncate(int fd, off_t len)            { return (int)sc(SYS_ftruncate, fd, len, 0); }
int     kill(pid_t pid, int sig)                { return (int)sc(SYS_kill, pid, sig, 0); }
pid_t   waitpid(pid_t p, int *st, int opt)      { return (pid_t)sc(SYS_waitpid, p, (long)st, opt); }
pid_t   wait(int *st)                           { return waitpid(-1, st, 0); }

int execve(const char *path, char *const argv[], char *const envp[])
{
    return (int)sc(SYS_execve, (long)path, (long)argv, (long)envp);
}

int execv(const char *path, char *const argv[])
{
    return execve(path, argv, 0);
}

int open(const char *path, int flags, ...)
{
    return (int)sc(SYS_open, (long)path, flags, 0);
}

int creat(const char *path, mode_t mode)
{
    (void)mode;
    return open(path, O_WRONLY | O_CREAT | O_TRUNC);
}

void *sbrk(intptr_t incr)
{
    long r = kiwi_syscall(SYS_sbrk, incr, 0, 0, 0, 0);
    if ((unsigned long)r >= (unsigned long)-4095) {
        errno = (int)-r;
        return (void *)-1;
    }
    return (void *)r;
}

char *getcwd(char *buf, size_t n)
{
    return sc(SYS_getcwd, (long)buf, (long)n, 0) < 0 ? 0 : buf;
}

static void conv_stat(const struct kiwi_stat *k, struct stat *st)
{
    st->st_ino = k->ino;
    st->st_mode = k->mode;
    st->st_size = (off_t)k->size;
    st->st_csize = (off_t)k->csize;
    st->st_nlink = k->nlink;
    st->st_mtime = k->mtime;
}

int stat(const char *path, struct stat *st)
{
    struct kiwi_stat k;
    if (sc(SYS_stat, (long)path, (long)&k, 0) < 0)
        return -1;
    conv_stat(&k, st);
    return 0;
}

int fstat(int fd, struct stat *st)
{
    struct kiwi_stat k;
    if (sc(SYS_fstat, fd, (long)&k, 0) < 0)
        return -1;
    conv_stat(&k, st);
    return 0;
}

int ioctl(int fd, int req, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, req);
    long arg = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    return (int)sc(SYS_ioctl, fd, req, arg);
}

int isatty(int fd)
{
    return kiwi_syscall(SYS_ioctl, fd, TTY_ISATTY, 0, 0, 0) == 1;
}

int uname(struct utsname *u)
{
    return (int)sc(SYS_uname, (long)u, 0, 0);
}

unsigned sleep(unsigned sec)
{
    kiwi_sleep_ms(sec * 1000);
    return 0;
}

int usleep(unsigned usec)
{
    return kiwi_sleep_ms(usec / 1000);
}

/* ---- Kiwi extensions ---- */
int      kz_stat(struct kiwi_kzstat *st)           { return (int)sc(SYS_kzstat, (long)st, 0, 0); }
int      kz_ctl(int op, int arg)                   { return (int)sc(SYS_kzctl, op, arg, 0); }
int      kiwi_ps(struct kiwi_procinfo *b, int max) { return (int)sc(SYS_ps, (long)b, max, 0); }
pid_t    kiwi_spawn(const struct kiwi_spawn *sp)   { return (pid_t)sc(SYS_spawn, (long)sp, 0, 0); }
int      kiwi_poweroff(void)                       { return (int)sc(SYS_poweroff, 0, 0, 0); }
uint32_t kiwi_uptime_ms(void)                      { return (uint32_t)kiwi_syscall(SYS_uptime, 0, 0, 0, 0, 0); }
int      kiwi_sleep_ms(unsigned ms)                { return (int)sc(SYS_sleep, (long)ms, 0, 0); }
int      tty_set_raw(int fd, int raw)              { return ioctl(fd, TTY_SETRAW, raw); }
int      tty_set_fg(int fd, pid_t pgid)            { return ioctl(fd, TTY_SETFG, pgid); }

int tty_get_size(int fd, int *rows, int *cols)
{
    int r = ioctl(fd, TTY_GETSIZE, 0);
    if (r < 0)
        return -1;
    *rows = r >> 16;
    *cols = r & 0xffff;
    return 0;
}

/* ---- dirent ---- */
struct _kiwi_dir {
    int fd;
    uint32_t idx;
    struct dirent ent;
};

DIR *opendir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return 0;
    DIR *d = malloc(sizeof *d);
    if (!d) {
        close(fd);
        errno = ENOMEM;
        return 0;
    }
    d->fd = fd;
    d->idx = 0;
    return d;
}

struct dirent *readdir(DIR *d)
{
    struct kiwi_dirent k;
    if (sc(SYS_readdir, d->fd, (long)d->idx, (long)&k) <= 0)
        return 0;
    d->idx++;
    d->ent.d_ino = k.d_ino;
    d->ent.d_type = k.d_type;
    d->ent.d_size = k.d_size;
    d->ent.d_csize = k.d_csize;
    memcpy(d->ent.d_name, k.d_name, sizeof d->ent.d_name);
    return &d->ent;
}

int closedir(DIR *d)
{
    int r = close(d->fd);
    free(d);
    return r;
}

/* ---- memory mapping, low-level access ---- */
#include <sys/mman.h>
#include <sys/io.h>
#include <termios.h>

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    struct kiwi_mmap m = { (uint32_t)addr, (uint32_t)len, prot, flags, fd, (uint32_t)off };
    long r = kiwi_syscall(SYS_mmap, (long)&m, 0, 0, 0, 0);
    if ((unsigned long)r >= (unsigned long)-4095) {
        errno = (int)-r;
        return MAP_FAILED;
    }
    return (void *)r;
}

int munmap(void *addr, size_t len) { return (int)sc(SYS_munmap, (long)addr, (long)len, 0); }
int iopl(int level)                { return (int)sc(SYS_iopl, level, 0, 0); }
uint32_t port_in(int size, uint16_t port)
{
    return (uint32_t)kiwi_syscall(SYS_portio, size == 4 ? PIO_INL : size == 2 ? PIO_INW : PIO_INB, port, 0, 0, 0);
}
void port_out(int size, uint16_t port, uint32_t v)
{
    kiwi_syscall(SYS_portio, size == 4 ? PIO_OUTL : size == 2 ? PIO_OUTW : PIO_OUTB, port, (long)v, 0, 0);
}
int kinfo(int what, int arg, void *buf, size_t len)
{
    long r = kiwi_syscall(SYS_kinfo, what, arg, (long)buf, (long)len, 0);
    if (r < 0 && r > -4096) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}
int kiwi_peek(uint32_t phys, void *buf, size_t len) { return (int)sc(SYS_peek, (long)phys, (long)buf, (long)len); }

/* ---- termios on top of the cooked/raw console ---- */
int tcgetattr(int fd, struct termios *t)
{
    int raw = ioctl(fd, TTY_GETRAW, 0);
    if (raw < 0)
        return -1;
    memset(t, 0, sizeof *t);
    if (!raw)
        t->c_lflag = ICANON | ECHO | ISIG;
    t->c_cc[VMIN] = 1;
    return 0;
}

int tcsetattr(int fd, int when, const struct termios *t)
{
    (void)when;
    return ioctl(fd, TTY_SETRAW, (t->c_lflag & ICANON) ? 0 : 1) < 0 ? -1 : 0;
}

void cfmakeraw(struct termios *t)
{
    t->c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG);
    t->c_iflag &= ~(tcflag_t)(ICRNL | IXON);
}
