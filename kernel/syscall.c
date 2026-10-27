/* System call table.  User pointers are range-checked, then accessed
 * directly: faults on compressed or demand-zero pages are serviced. */
#include "kernel.h"

int pipe_create(struct file **rf, struct file **wf);
int pipe_read(struct file *f, uint8_t *buf, uint32_t n);
int pipe_write(struct file *f, const uint8_t *buf, uint32_t n);

int user_ok(const void *p, size_t n)
{
    uint32_t a = (uint32_t)p;
    return a >= USER_BASE && a < USER_END && n <= USER_END - a;
}

int copy_from_user(void *dst, const void *usrc, size_t n)
{
    if (!user_ok(usrc, n))
        return -EFAULT;
    memcpy(dst, usrc, n);
    return 0;
}

int copy_to_user(void *udst, const void *src, size_t n)
{
    if (!user_ok(udst, n))
        return -EFAULT;
    memcpy(udst, src, n);
    return 0;
}

int copy_str_from_user(char *dst, const char *usrc, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (!user_ok(usrc + i, 1))
            return -EFAULT;
        dst[i] = usrc[i];
        if (!dst[i])
            return 0;
    }
    return -ENAMETOOLONG;
}

static struct file *getf(int fd)
{
    if (fd < 0 || fd >= NFD)
        return 0;
    return current->fds[fd];
}

static int upath(char *dst, uint32_t up)
{
    return copy_str_from_user(dst, (const char *)up, 256);
}

static int sys_read(int fd, uint8_t *buf, uint32_t n)
{
    struct file *f = getf(fd);
    if (!f)
        return -EBADF;
    if (!user_ok(buf, n))
        return -EFAULT;
    int r;
    switch (f->type) {
    case F_CONSOLE:
        return tty_read((char *)buf, n);
    case F_FILE:
        if ((f->flags & O_ACCMODE) == O_WRONLY)
            return -EBADF;
        r = ramfs_rw(f->ino, f->off, buf, n, 0);
        if (r > 0)
            f->off += (uint32_t)r;
        return r;
    case F_PIPE_R:
        return pipe_read(f, buf, n);
    case F_DIR:
        return -EISDIR;
    case F_DEV: {
        const struct devops *d = dev_ops(f->dev);
        return d && d->read ? d->read(f, buf, n) : -EINVAL;
    }
    }
    return -EBADF;
}

static int sys_write(int fd, const uint8_t *buf, uint32_t n)
{
    struct file *f = getf(fd);
    if (!f)
        return -EBADF;
    if (!user_ok(buf, n))
        return -EFAULT;
    int r;
    switch (f->type) {
    case F_CONSOLE:
        console_write((const char *)buf, n);
        return (int)n;
    case F_FILE: {
        if ((f->flags & O_ACCMODE) == O_RDONLY)
            return -EBADF;
        if (f->flags & O_APPEND) {
            struct kiwi_stat st;
            ramfs_stat(f->ino, &st);
            f->off = st.size;
        }
        r = ramfs_rw(f->ino, f->off, (void *)buf, n, 1);
        if (r > 0)
            f->off += (uint32_t)r;
        return r;
    }
    case F_PIPE_W:
        r = pipe_write(f, buf, n);
        if (r == -EPIPE)
            current->killed = 13;       /* SIGPIPE */
        return r;
    case F_DEV: {
        const struct devops *d = dev_ops(f->dev);
        return d && d->write ? d->write(f, buf, n) : -EINVAL;
    }
    }
    return -EBADF;
}

static int sys_open(uint32_t upath_p, int flags)
{
    char path[256];
    int e = upath(path, upath_p);
    if (e < 0)
        return e;
    struct inode *ino = ramfs_lookup(path, current->cwd);
    if (ino && (flags & O_CREAT) && (flags & O_EXCL))
        return -EEXIST;
    if (!ino) {
        if (!(flags & O_CREAT))
            return -ENOENT;
        ino = ramfs_create(path, current->cwd, 0, &e);
        if (!ino)
            return e;
    }
    int dir = ramfs_isdir(ino), devno = ramfs_devno(ino);
    if (dir && (flags & O_ACCMODE) != O_RDONLY)
        return -EISDIR;
    if (!dir && (flags & O_DIRECTORY))
        return -ENOTDIR;
    struct file *f = kzalloc(sizeof *f);
    if (!f)
        return -ENOMEM;
    f->type = dir ? F_DIR : devno >= 0 ? F_DEV : F_FILE;
    f->dev = devno;
    f->refs = 1;
    f->flags = flags;
    f->ino = ino;
    if ((e = ramfs_open_inode(ino)) < 0) {
        kfree(f);
        return e;
    }
    if (devno >= 0) {
        const struct devops *d = dev_ops(devno);
        if (d && d->open && (e = d->open(f)) < 0) {
            ramfs_close_inode(ino);
            kfree(f);
            return e;
        }
    }
    if (!dir && (flags & O_TRUNC) && (flags & O_ACCMODE) != O_RDONLY)
        ramfs_truncate(ino, 0);
    int fd = fd_alloc(current, f);
    if (fd < 0)
        file_close(f);
    return fd;
}

static int sys_close(int fd)
{
    struct file *f = getf(fd);
    if (!f)
        return -EBADF;
    current->fds[fd] = 0;
    file_close(f);
    return 0;
}

static int sys_lseek(int fd, int off, int whence)
{
    struct file *f = getf(fd);
    if (!f)
        return -EBADF;
    if (f->type == F_DEV && whence == SEEK_SET && off >= 0)
        return (int)(f->off = (uint32_t)off);
    if (f->type != F_FILE)
        return -ESPIPE;
    struct kiwi_stat st;
    ramfs_stat(f->ino, &st);
    int base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (int)f->off :
               whence == SEEK_END ? (int)st.size : -1;
    if (base < 0 || base + off < 0)
        return -EINVAL;
    f->off = (uint32_t)(base + off);
    return (int)f->off;
}

static int sys_fstat(int fd, struct kiwi_stat *ust)
{
    struct file *f = getf(fd);
    if (!f)
        return -EBADF;
    struct kiwi_stat st;
    memset(&st, 0, sizeof st);
    if (f->ino)
        ramfs_stat(f->ino, &st);
    else
        st.mode = f->type == F_CONSOLE ? S_IFCHR | 0620 : S_IFIFO | 0600;
    return copy_to_user(ust, &st, sizeof st);
}

static int sys_stat(uint32_t up, struct kiwi_stat *ust)
{
    char path[256];
    int e = upath(path, up);
    if (e < 0)
        return e;
    struct inode *ino = ramfs_lookup(path, current->cwd);
    if (!ino)
        return -ENOENT;
    struct kiwi_stat st;
    ramfs_stat(ino, &st);
    return copy_to_user(ust, &st, sizeof st);
}

static int sys_readdir(int fd, uint32_t idx, struct kiwi_dirent *ude)
{
    struct file *f = getf(fd);
    if (!f)
        return -EBADF;
    if (f->type != F_DIR)
        return -ENOTDIR;
    struct kiwi_dirent de;
    memset(&de, 0, sizeof de);
    int r = ramfs_readdir(f->ino, idx, &de);
    if (r == 1 && copy_to_user(ude, &de, sizeof de) < 0)
        return -EFAULT;
    return r;
}

static int sys_mkdir(uint32_t up)
{
    char path[256];
    int e = upath(path, up);
    if (e < 0)
        return e;
    ramfs_create(path, current->cwd, 1, &e);
    return e;
}

static int sys_unlink(uint32_t up)
{
    char path[256];
    int e = upath(path, up);
    return e < 0 ? e : ramfs_unlink(path, current->cwd);
}

static int sys_rename(uint32_t ua, uint32_t ub)
{
    char a[256], b[256];
    int e;
    if ((e = upath(a, ua)) < 0 || (e = upath(b, ub)) < 0)
        return e;
    return ramfs_rename(a, b, current->cwd);
}

static int sys_chdir(uint32_t up)
{
    char path[256], norm[256];
    int e = upath(path, up);
    if (e < 0)
        return e;
    struct inode *ino = ramfs_lookup(path, current->cwd);
    if (!ino)
        return -ENOENT;
    if (!ramfs_isdir(ino))
        return -ENOTDIR;
    if ((e = path_normalize(path, current->cwd, norm, sizeof norm)) < 0)
        return e;
    strlcpy(current->cwd, norm, sizeof current->cwd);
    return 0;
}

static int sys_getcwd(char *ubuf, uint32_t n)
{
    uint32_t l = strlen(current->cwd) + 1;
    if (l > n)
        return -ERANGE;
    return copy_to_user(ubuf, current->cwd, l) < 0 ? -EFAULT : (int)l;
}

static int sys_ioctl(int fd, int req, uint32_t arg)
{
    struct file *f = getf(fd);
    if (!f)
        return -EBADF;
    if (f->type == F_DEV) {
        const struct devops *d = dev_ops(f->dev);
        return d && d->ioctl ? d->ioctl(f, req, arg) : -ENOTTY;
    }
    if (f->type != F_CONSOLE)
        return -ENOTTY;
    return tty_ioctl(req, arg);
}

static int sys_kzctl(int op, uint32_t arg)
{
    switch (op) {
    case KZ_SET_BUDGET:
        zram_set_budget(arg);
        return (int)zram_budget;
    case KZ_GET_BUDGET:
        return (int)zram_budget;
    case KZ_COMPRESS_ALL:
        return zram_compress_all();
    case KZ_SET_PROACTIVE:
        zram_proactive = arg;
        return 0;
    }
    return -EINVAL;
}

static int sys_sleep(uint32_t ms)
{
    current->wake_tick = ticks + (ms + 9) / 10 + 1;
    current->wait_chan = &current->wake_tick;
    current->state = P_BLOCKED;
    schedule();
    current->wait_chan = 0;
    return current->killed ? -EINTR : 0;
}

static int sys_uname(struct kiwi_utsname *u)
{
    struct kiwi_utsname k;
    memset(&k, 0, sizeof k);
    strlcpy(k.sysname, KIWI_NAME, sizeof k.sysname);
    strlcpy(k.nodename, "kiwi", sizeof k.nodename);
    strlcpy(k.release, KIWI_RELEASE, sizeof k.release);
    strlcpy(k.version, "zram+ramfs-lz " __DATE__ " " __TIME__, sizeof k.version);
    strlcpy(k.machine, "i686", sizeof k.machine);
    return copy_to_user(u, &k, sizeof k);
}

static int sys_dup2(int oldfd, int newfd)
{
    struct file *f = getf(oldfd);
    if (!f || newfd < 0 || newfd >= NFD)
        return -EBADF;
    if (oldfd == newfd)
        return newfd;
    if (current->fds[newfd])
        file_close(current->fds[newfd]);
    current->fds[newfd] = file_dup(f);
    return newfd;
}

static int sys_dup(int fd)
{
    struct file *f = getf(fd);
    if (!f)
        return -EBADF;
    int n = fd_alloc(current, f);
    if (n >= 0)
        file_dup(f);
    return n;
}

static int sys_pipe(int *ufds)
{
    struct file *rf, *wf;
    if (!user_ok(ufds, 8))
        return -EFAULT;
    int e = pipe_create(&rf, &wf);
    if (e < 0)
        return e;
    int a = fd_alloc(current, rf);
    int b = a >= 0 ? fd_alloc(current, wf) : -EMFILE;
    if (b < 0) {
        if (a >= 0)
            current->fds[a] = 0;
        file_close(rf);
        file_close(wf);
        return -EMFILE;
    }
    int fds[2] = { a, b };
    return copy_to_user(ufds, fds, sizeof fds);
}

static int sys_spawn(const struct kiwi_spawn *usp)
{
    struct kiwi_spawn sp;
    if (copy_from_user(&sp, usp, sizeof sp) < 0)
        return -EFAULT;
    return proc_spawn(&sp);
}

static int sys_setpgid(int pid, int pgid)
{
    struct proc *p = pid ? proc_by_pid(pid) : current;
    if (!p)
        return -ESRCH;
    p->pgid = pgid ? pgid : p->pid;
    return 0;
}

static int sys_ftruncate(int fd, uint32_t len)
{
    struct file *f = getf(fd);
    if (!f)
        return -EBADF;
    if (f->type != F_FILE || (f->flags & O_ACCMODE) == O_RDONLY)
        return -EINVAL;
    return ramfs_truncate(f->ino, len);
}

void syscall_dispatch(struct regs *r)
{
    uint32_t a = r->ebx, b = r->ecx, c = r->edx, d = r->esi;
    int ret;
    switch (r->eax) {
    case SYS_exit:     do_exit((int)((a & 0xff) << 8));
    case SYS_fork:     ret = proc_fork(r); break;
    case SYS_read:     ret = sys_read((int)a, (uint8_t *)b, c); break;
    case SYS_write:    ret = sys_write((int)a, (const uint8_t *)b, c); break;
    case SYS_open:     ret = sys_open(a, (int)b); break;
    case SYS_close:    ret = sys_close((int)a); break;
    case SYS_waitpid:  ret = proc_waitpid((int)a, (int *)b, (int)c); break;
    case SYS_execve: {
        char path[256];
        ret = upath(path, a);
        if (ret == 0)
            ret = proc_execve(r, path, (char *const *)b);
        if (ret == 0)
            return;                     /* registers already replaced */
        break;
    }
    case SYS_getpid:   ret = current->pid; break;
    case SYS_sbrk:     ret = proc_sbrk((int)a); break;
    case SYS_lseek:    ret = sys_lseek((int)a, (int)b, (int)c); break;
    case SYS_stat:     ret = sys_stat(a, (struct kiwi_stat *)b); break;
    case SYS_fstat:    ret = sys_fstat((int)a, (struct kiwi_stat *)b); break;
    case SYS_readdir:  ret = sys_readdir((int)a, b, (struct kiwi_dirent *)c); break;
    case SYS_mkdir:    ret = sys_mkdir(a); break;
    case SYS_unlink:   ret = sys_unlink(a); break;
    case SYS_chdir:    ret = sys_chdir(a); break;
    case SYS_getcwd:   ret = sys_getcwd((char *)a, b); break;
    case SYS_uptime:   ret = (int)(ticks * (1000 / HZ)); break;
    case SYS_ioctl:    ret = sys_ioctl((int)a, (int)b, c); break;
    case SYS_kzstat: {
        struct kiwi_kzstat st;
        zram_stat(&st);
        ret = copy_to_user((void *)a, &st, sizeof st);
        break;
    }
    case SYS_kzctl:    ret = sys_kzctl((int)a, b); break;
    case SYS_sleep:    ret = sys_sleep(a); break;
    case SYS_uname:    ret = sys_uname((struct kiwi_utsname *)a); break;
    case SYS_rename:   ret = sys_rename(a, b); break;
    case SYS_dup2:     ret = sys_dup2((int)a, (int)b); break;
    case SYS_pipe:     ret = sys_pipe((int *)a); break;
    case SYS_ps:       ret = proc_list((struct kiwi_procinfo *)a, (int)b); break;
    case SYS_kill:
        if ((int)a < 0) {
            proc_kill_pgrp(-(int)a);
            ret = 0;
        } else {
            ret = proc_kill((int)a, (int)b);
        }
        break;
    case SYS_time:     ret = (int)(boot_epoch + ticks / HZ); break;
    case SYS_getppid:  ret = current->ppid; break;
    case SYS_spawn:    ret = sys_spawn((const struct kiwi_spawn *)a); break;
    case SYS_setpgid:  ret = sys_setpgid((int)a, (int)b); break;
    case SYS_dup:      ret = sys_dup((int)a); break;
    case SYS_poweroff: poweroff();
    case SYS_ftruncate: ret = sys_ftruncate((int)a, b); break;
    case SYS_mmap:     ret = sys_mmap((const struct kiwi_mmap *)a); break;
    case SYS_munmap:   ret = sys_munmap(a, b); break;
    case SYS_portio:   ret = sys_portio((int)a, b, c); break;
    case SYS_iopl:
        if (a > 3) {
            ret = -EINVAL;
        } else {
            /* IOPL lives in EFLAGS: in/out become legal in ring 3 */
            r->eflags = (r->eflags & ~0x3000u) | (a << 12);
            current->iopl = (int)a;
            ret = 0;
        }
        break;
    case SYS_kinfo:    ret = sys_kinfo((int)a, (int)b, (uint8_t *)c, d); break;
    case SYS_peek:     ret = sys_peek(a, (uint8_t *)b, c); break;
    default:           ret = -ENOSYS;
    }
    r->eax = (uint32_t)ret;
}
