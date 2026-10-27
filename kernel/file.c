/* Open file objects shared between descriptors, and pipes. */
#include "kernel.h"

#define PIPE_SIZE 4096

struct pipe {
    uint8_t  buf[PIPE_SIZE];
    uint32_t r, w;
    int      readers, writers;
};

static struct file *file_new(int type)
{
    struct file *f = kzalloc(sizeof *f);
    if (f) {
        f->type = type;
        f->refs = 1;
    }
    return f;
}

struct file *file_console(void)
{
    return file_new(F_CONSOLE);
}

struct file *file_dup(struct file *f)
{
    f->refs++;
    return f;
}

void file_close(struct file *f)
{
    if (--f->refs > 0)
        return;
    if (f->type == F_DEV) {
        const struct devops *d = dev_ops(f->dev);
        if (d && d->close)
            d->close(f);
        ramfs_close_inode(f->ino);
    } else if (f->type == F_FILE || f->type == F_DIR) {
        ramfs_close_inode(f->ino);
    } else if (f->type == F_PIPE_R || f->type == F_PIPE_W) {
        struct pipe *p = f->pipe;
        if (f->type == F_PIPE_R)
            p->readers--;
        else
            p->writers--;
        wakeup(p);
        if (!p->readers && !p->writers)
            kfree(p);
    }
    kfree(f);
}

int fd_alloc(struct proc *p, struct file *f)
{
    for (int i = 0; i < NFD; i++) {
        if (!p->fds[i]) {
            p->fds[i] = f;
            return i;
        }
    }
    return -EMFILE;
}

int pipe_create(struct file **rf, struct file **wf)
{
    struct pipe *p = kzalloc(sizeof *p);
    *rf = file_new(F_PIPE_R);
    *wf = file_new(F_PIPE_W);
    if (!p || !*rf || !*wf) {
        kfree(p);
        kfree(*rf);
        kfree(*wf);
        return -ENOMEM;
    }
    p->readers = p->writers = 1;
    (*rf)->pipe = (*wf)->pipe = p;
    return 0;
}

/* buf is a user pointer */
int pipe_read(struct file *f, uint8_t *buf, uint32_t n)
{
    struct pipe *p = f->pipe;
    while (p->r == p->w) {
        if (!p->writers)
            return 0;
        sleep_on(p);
        if (current->killed)
            return -EINTR;
    }
    uint32_t got = 0;
    while (got < n && p->r != p->w)
        buf[got++] = p->buf[p->r++ % PIPE_SIZE];
    wakeup(p);
    return (int)got;
}

int pipe_write(struct file *f, const uint8_t *buf, uint32_t n)
{
    struct pipe *p = f->pipe;
    uint32_t put = 0;
    while (put < n) {
        if (!p->readers)
            return put ? (int)put : -EPIPE;
        if (p->w - p->r == PIPE_SIZE) {
            wakeup(p);
            sleep_on(p);
            if (current->killed)
                return put ? (int)put : -EINTR;
            continue;
        }
        p->buf[p->w++ % PIPE_SIZE] = buf[put++];
    }
    wakeup(p);
    return (int)put;
}
