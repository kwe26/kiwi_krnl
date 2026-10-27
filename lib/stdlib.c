/* stdlib.h: malloc on sbrk (shared allocator), conversions, qsort, exit. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <malloc.h>
#include <assert.h>
#include "alloc.h"

static struct aheap heap;
static int heap_ready;

static void *morecore(struct aheap *h, size_t incr)
{
    incr = (incr + 65535) & ~(size_t)65535;     /* grow in 64KB steps */
    void *p = sbrk((intptr_t)incr);
    if (p == (void *)-1)
        return 0;
    if ((char *)p != h->end)
        return 0;                               /* someone else moved brk */
    h->end += incr;
    return p;
}

static void heap_init(void)
{
    void *start = sbrk(65536);
    if (start == (void *)-1) {
        write(2, "malloc: no heap\n", 16);
        _exit(127);
    }
    aheap_init(&heap, start, 65536, morecore);
    heap_ready = 1;
}

void *malloc(size_t n)
{
    if (!heap_ready)
        heap_init();
    void *p = aheap_alloc(&heap, n);
    if (!p)
        errno = ENOMEM;
    return p;
}

void free(void *p)
{
    if (p)
        aheap_free(&heap, p);
}

void *calloc(size_t n, size_t sz)
{
    if (sz && n > (size_t)-1 / sz)
        return 0;
    void *p = malloc(n * sz);
    if (p)
        memset(p, 0, n * sz);
    return p;
}

void *realloc(void *p, size_t n)
{
    if (!heap_ready)
        heap_init();
    return aheap_realloc(&heap, p, n);
}

size_t malloc_usable_size(void *p)
{
    return p ? aheap_usable(p) : 0;
}

/* ---- process ---- */
static void (*atexit_fns[32])(void);
static int natexit;

int atexit(void (*fn)(void))
{
    if (natexit >= 32)
        return -1;
    atexit_fns[natexit++] = fn;
    return 0;
}

void exit(int status)
{
    while (natexit > 0)
        atexit_fns[--natexit]();
    fflush(stdout);
    fflush(stderr);
    _exit(status);
}

void abort(void)
{
    fflush(stdout);
    write(2, "abort()\n", 8);
    _exit(134);
}

void __kiwi_assert_fail(const char *expr, const char *file, int line, const char *fn)
{
    fprintf(stderr, "assertion failed: %s (%s:%d %s)\n", expr, file, line, fn);
    abort();
}

int main(int argc, char **argv, char **envp);

void __libc_main(int argc, char **argv, char **envp)
{
    exit(main(argc, argv, envp));
}

char *getenv(const char *name)
{
    (void)name;
    return 0;
}

/* ---- numbers ---- */
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

unsigned long long strtoull(const char *s, char **end, int base)
{
    const char *p = s;
    while (isspace((unsigned char)*p))
        p++;
    int neg = 0;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] | 32) == 'x' && isxdigit((unsigned char)p[2])) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = *p == '0' ? 8 : 10;
    }
    unsigned long long v = 0;
    const char *start = p;
    for (;; p++) {
        int c = (unsigned char)*p, d;
        if (isdigit(c)) d = c - '0';
        else if (isalpha(c)) d = (c | 32) - 'a' + 10;
        else break;
        if (d >= base)
            break;
        v = v * (unsigned)base + (unsigned)d;
    }
    if (end)
        *end = (char *)(p == start ? s : p);
    return neg ? -v : v;
}

long long strtoll(const char *s, char **end, int base)
{
    return (long long)strtoull(s, end, base);
}

unsigned long strtoul(const char *s, char **end, int base)
{
    return (unsigned long)strtoull(s, end, base);
}

long strtol(const char *s, char **end, int base)
{
    long long v = strtoll(s, end, base);
    if (v > LONG_MAX) { errno = ERANGE; return LONG_MAX; }
    if (v < LONG_MIN) { errno = ERANGE; return LONG_MIN; }
    return (long)v;
}

int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }

double strtod(const char *s, char **end)
{
    const char *p = s;
    while (isspace((unsigned char)*p))
        p++;
    int neg = 0;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if (strncasecmp(p, "inf", 3) == 0) {
        p += strncasecmp(p, "infinity", 8) == 0 ? 8 : 3;
        if (end) *end = (char *)p;
        return neg ? -__builtin_inf() : __builtin_inf();
    }
    if (strncasecmp(p, "nan", 3) == 0) {
        if (end) *end = (char *)(p + 3);
        return __builtin_nan("");
    }
    long double v = 0;
    int digits = 0, ex = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p++ - '0');
        digits++;
    }
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p)) {
            v = v * 10 + (*p++ - '0');
            ex--;
            digits++;
        }
    }
    if (!digits) {
        if (end) *end = (char *)s;
        return 0;
    }
    if ((*p | 32) == 'e') {
        const char *q = p + 1;
        int eneg = 0, e = 0;
        if (*q == '+' || *q == '-')
            eneg = *q++ == '-';
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q))
                e = e * 10 + (*q++ - '0');
            ex += eneg ? -e : e;
            p = q;
        }
    }
    long double scale = 1, b = 10;
    int ae = ex < 0 ? -ex : ex;
    while (ae) {
        if (ae & 1) scale *= b;
        b *= b;
        ae >>= 1;
    }
    v = ex < 0 ? v / scale : v * scale;
    if (end)
        *end = (char *)p;
    return neg ? -(double)v : (double)v;
}

double atof(const char *s) { return strtod(s, 0); }

static unsigned long rand_state = 1;
int rand(void)
{
    rand_state = rand_state * 1103515245u + 12345u;
    return (int)((rand_state >> 1) & RAND_MAX);
}
void srand(unsigned seed) { rand_state = seed; }

/* ---- sorting ---- */
static void swap(char *a, char *b, size_t sz)
{
    while (sz--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    char *b = base;
    while (n > 1) {
        if (n < 8) {        /* insertion sort */
            for (size_t i = 1; i < n; i++)
                for (size_t j = i; j > 0 && cmp(b + (j - 1) * sz, b + j * sz) > 0; j--)
                    swap(b + (j - 1) * sz, b + j * sz, sz);
            return;
        }
        swap(b, b + (n / 2) * sz, sz);
        size_t lo = 1, hi = n - 1;
        for (;;) {
            while (lo <= hi && cmp(b + lo * sz, b) < 0) lo++;
            while (hi >= lo && cmp(b + hi * sz, b) > 0) hi--;
            if (lo >= hi) break;
            swap(b + lo * sz, b + hi * sz, sz);
            lo++;
            hi--;
        }
        swap(b, b + hi * sz, sz);
        /* recurse on the smaller half */
        if (hi < n - hi - 1) {
            qsort(b, hi, sz, cmp);
            b += (hi + 1) * sz;
            n -= hi + 1;
        } else {
            qsort(b + (hi + 1) * sz, n - hi - 1, sz, cmp);
            n = hi;
        }
    }
}

void *bsearch(const void *key, const void *base, size_t n, size_t sz,
              int (*cmp)(const void *, const void *))
{
    const char *b = base;
    while (n) {
        const char *m = b + (n / 2) * sz;
        int c = cmp(key, m);
        if (c == 0)
            return (void *)m;
        if (c > 0) {
            b = m + sz;
            n -= n / 2 + 1;
        } else {
            n /= 2;
        }
    }
    return 0;
}
