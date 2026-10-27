/*
 * Character devices under /dev: null zero kmsg fb0 input tty.
 */
#include "kernel.h"

static int null_read(struct file *f, uint8_t *u, uint32_t n) { return 0; }
static int null_write(struct file *f, const uint8_t *u, uint32_t n) { return (int)n; }

static int zero_read(struct file *f, uint8_t *u, uint32_t n)
{
    memset(u, 0, n);
    return (int)n;
}

static int kmsg_read(struct file *f, uint8_t *u, uint32_t n)
{
    char buf[512];
    uint32_t got = 0;
    while (got < n) {
        uint32_t c = klog_read(buf, f->off, n - got < sizeof buf ? n - got : sizeof buf);
        if (!c)
            break;
        memcpy(u + got, buf, c);
        got += c;
        f->off += c;
    }
    return (int)got;
}

static int kmsg_write(struct file *f, const uint8_t *u, uint32_t n)
{
    char buf[256];
    uint32_t c = n < sizeof buf - 1 ? n : sizeof buf - 1;
    memcpy(buf, u, c);
    buf[c] = 0;
    kprintf("%s", buf);
    return (int)n;
}

/* ---- fb0 ---- */
static int fb_ioctl(struct file *f, int req, uint32_t arg)
{
    struct kiwi_fb_info info;
    struct kiwi_fb_mode m;
    switch (req) {
    case FB_GET_INFO:
        fb_get_info(&info);
        return copy_to_user((void *)arg, &info, sizeof info);
    case FB_SET_MODE: {
        if (copy_from_user(&m, (void *)arg, sizeof m) < 0)
            return -EFAULT;
        int r = fb_set_mode(m.width, m.height, m.bpp);
        if (r == 0)
            f->devdata = 1;                     /* restore text mode on close */
        return r;
    }
    case FB_TEXT_MODE:
        fb_text_mode();
        f->devdata = 0;
        return 0;
    }
    return -ENOTTY;
}

static void fb_close(struct file *f)
{
    if (f->devdata)
        fb_text_mode();
}

static uint32_t fb_mmap(struct file *f, uint32_t off)
{
    struct kiwi_fb_info info;
    fb_get_info(&info);
    if (!info.phys || off >= info.vram)
        return 0;
    return info.phys + off;
}

/* ---- input ---- */
static int input_open(struct file *f)
{
    input_grab(1);
    f->devdata = 0;                             /* timeout: block */
    return 0;
}

static void input_close(struct file *f)
{
    input_grab(0);
}

static int input_dev_read(struct file *f, uint8_t *u, uint32_t n)
{
    return input_read(u, n, f->devdata);
}

static int input_ioctl(struct file *f, int req, uint32_t arg)
{
    if (req != INPUT_SET_TIMEOUT)
        return -ENOTTY;
    f->devdata = (int)arg;
    return 0;
}

/* ---- tty ---- */
static int tty_dev_read(struct file *f, uint8_t *u, uint32_t n) { return tty_read((char *)u, n); }
static int tty_dev_write(struct file *f, const uint8_t *u, uint32_t n)
{
    console_write((const char *)u, n);
    return (int)n;
}
static int tty_dev_ioctl(struct file *f, int req, uint32_t arg) { return tty_ioctl(req, arg); }

static const struct devops devs[NDEV] = {
    [DEV_NULL]  = { "null", 0, 0, null_read, null_write, 0, 0 },
    [DEV_ZERO]  = { "zero", 0, 0, zero_read, null_write, 0, 0 },
    [DEV_KMSG]  = { "kmsg", 0, 0, kmsg_read, kmsg_write, 0, 0 },
    [DEV_FB]    = { "fb0", 0, fb_close, 0, 0, fb_ioctl, fb_mmap },
    [DEV_INPUT] = { "input", input_open, input_close, input_dev_read, 0, input_ioctl, 0 },
    [DEV_TTY]   = { "tty", 0, 0, tty_dev_read, tty_dev_write, tty_dev_ioctl, 0 },
};

const struct devops *dev_ops(int devno)
{
    return devno >= 0 && devno < NDEV ? &devs[devno] : 0;
}

void dev_init(void)
{
    int err;
    ramfs_create("/dev", "/", 1, &err);
    char path[32];
    for (int i = 0; i < NDEV; i++) {
        ksnprintf(path, sizeof path, "/dev/%s", devs[i].name);
        ramfs_mknod(path, i);
    }
}
