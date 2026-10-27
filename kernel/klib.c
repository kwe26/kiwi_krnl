/* Minimal string + formatting routines for the kernel. */
#include "kernel.h"

void *memcpy(void *d, const void *s, size_t n)
{
    void *ret = d;
    __asm__ volatile("rep movsl\n\t"
                     "mov %3, %%ecx\n\t"
                     "rep movsb"
                     : "+D"(d), "+S"(s), "=c"(n)
                     : "r"(n & 3), "2"(n >> 2)
                     : "memory");
    return ret;
}

void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *dp = d;
    const uint8_t *sp = s;
    if (dp < sp || dp >= sp + n)
        return memcpy(d, s, n);
    while (n--)
        dp[n] = sp[n];
    return d;
}

void *memset(void *d, int c, size_t n)
{
    void *ret = d;
    uint32_t v = (uint8_t)c * 0x01010101u;
    __asm__ volatile("rep stosl\n\t"
                     "mov %3, %%ecx\n\t"
                     "rep stosb"
                     : "+D"(d), "=c"(n)
                     : "a"(v), "r"(n & 3), "1"(n >> 2)
                     : "memory");
    return ret;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (uint8_t)*a - (uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (uint8_t)*a - (uint8_t)*b;
        if (!*a)
            return 0;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c ? 0 : (char *)s;
}

size_t strlcpy(char *d, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (n) {
        size_t c = l < n - 1 ? l : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return l;
}

/* ---- formatting ---- */
struct out { char *buf; size_t n, pos; };

static void put(struct out *o, char c)
{
    if (o->pos + 1 < o->n)
        o->buf[o->pos] = c;
    o->pos++;
}

int kvsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    struct out o = { buf, n, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        fmt++;
        int zero = 0, left = 0, width = 0, lng = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') { lng++; fmt++; }
        char tmp[24];
        int len = 0;
        const char *s = tmp;
        switch (*fmt) {
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            len = (int)strlen(s);
            break;
        case 'd': case 'u': case 'x': case 'p': {
            uint64_t v;
            int neg = 0, base = *fmt == 'd' || *fmt == 'u' ? 10 : 16;
            if (lng >= 2)
                v = va_arg(ap, uint64_t);
            else
                v = va_arg(ap, uint32_t);
            if (*fmt == 'd') {
                int64_t sv = lng >= 2 ? (int64_t)v : (int64_t)(int32_t)v;
                if (sv < 0) { neg = 1; v = (uint64_t)-sv; }
            }
            if (*fmt == 'p') { zero = 1; width = 8; }
            char r[24];
            int rl = 0;
            do {
                uint32_t dg = (uint32_t)(v % base);
                r[rl++] = (char)(dg < 10 ? '0' + dg : 'a' + dg - 10);
                v /= base;
            } while (v);
            if (neg) tmp[len++] = '-';
            while (rl) tmp[len++] = r[--rl];
            break;
        }
        case '%':
            tmp[0] = '%';
            len = 1;
            break;
        default:
            tmp[0] = '?';
            len = 1;
        }
        int pad = width > len ? width - len : 0;
        if (!left)
            while (pad-- > 0) put(&o, zero ? '0' : ' ');
        for (int i = 0; i < len; i++)
            put(&o, s[i]);
        if (left)
            while (pad-- > 0) put(&o, ' ');
    }
    if (o.n)
        o.buf[o.pos < o.n ? o.pos : o.n - 1] = 0;
    return (int)o.pos;
}

int ksnprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = kvsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

/* ---- kernel log (dmesg): a ring of everything kprintf printed ---- */
#define KLOG_SIZE 16384
static char klog[KLOG_SIZE];
static uint32_t klog_pos;       /* total bytes ever written */

static void klog_write(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        klog[klog_pos++ % KLOG_SIZE] = s[i];
}

/* copy the log, oldest first; returns bytes copied (from byte offset off) */
uint32_t klog_read(char *dst, uint32_t off, uint32_t n)
{
    uint32_t start = klog_pos > KLOG_SIZE ? klog_pos - KLOG_SIZE : 0;
    uint32_t avail = klog_pos - start;
    if (off >= avail)
        return 0;
    if (n > avail - off)
        n = avail - off;
    for (uint32_t i = 0; i < n; i++)
        dst[i] = klog[(start + off + i) % KLOG_SIZE];
    return n;
}

void kprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t len = n < (int)sizeof buf ? (size_t)n : sizeof buf - 1;
    klog_write(buf, len);
    console_write(buf, len);
}

void panic(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    cli();
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    kprintf("\n\033[1;31m*** KERNEL PANIC: %s\033[0m\n", buf);
    for (;;)
        hlt();
}
