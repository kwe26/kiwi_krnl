/* stdio.h: buffered FILE streams and a printf engine (with %f/%e/%g). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>

struct _kiwi_file {
    int fd;
    int flags;          /* 1 read, 2 write */
    int eof, err;
    int unbuffered;
    int linebuf;
    char *buf;
    size_t len, pos, cap;   /* write: len used; read: pos..len */
    int reading;
};

static char outbuf[1024], errbuf_[1];
static char inbuf[1024];
static FILE f_stdin  = { 0, 1, 0, 0, 0, 0, inbuf, 0, 0, sizeof inbuf, 1 };
static FILE f_stdout = { 1, 2, 0, 0, 0, 1, outbuf, 0, 0, sizeof outbuf, 0 };
static FILE f_stderr = { 2, 2, 0, 0, 1, 0, errbuf_, 0, 0, 0, 0 };
FILE *stdin = &f_stdin, *stdout = &f_stdout, *stderr = &f_stderr;

static int write_all(int fd, const char *p, size_t n)
{
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w <= 0)
            return -1;
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

int fflush(FILE *f)
{
    if (!f) {
        fflush(stdout);
        fflush(stderr);
        return 0;
    }
    if (!f->reading && f->len) {
        int r = write_all(f->fd, f->buf, f->len);
        f->len = 0;
        if (r < 0) {
            f->err = 1;
            return EOF;
        }
    }
    return 0;
}

size_t fwrite(const void *p, size_t sz, size_t n, FILE *f)
{
    size_t total = sz * n;
    const char *s = p;
    if (!total)
        return 0;
    if (f->reading) {
        f->reading = 0;
        f->len = f->pos = 0;
    }
    if (f->unbuffered || !f->cap) {
        if (write_all(f->fd, s, total) < 0) {
            f->err = 1;
            return 0;
        }
        return n;
    }
    for (size_t i = 0; i < total; i++) {
        f->buf[f->len++] = s[i];
        if (f->len == f->cap || (f->linebuf && s[i] == '\n'))
            if (fflush(f) == EOF)
                return i / sz;
    }
    return n;
}

int fputc(int c, FILE *f)
{
    unsigned char ch = (unsigned char)c;
    return fwrite(&ch, 1, 1, f) == 1 ? ch : EOF;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *f)
{
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF)
        return EOF;
    return fputc('\n', stdout) == EOF ? EOF : 0;
}

static int fill(FILE *f)
{
    if (f == stdin)
        fflush(stdout);
    if (!f->reading) {
        fflush(f);
        f->reading = 1;
        f->len = f->pos = 0;
    }
    ssize_t r = read(f->fd, f->buf, f->cap);
    if (r <= 0) {
        if (r == 0) f->eof = 1; else f->err = 1;
        return EOF;
    }
    f->len = (size_t)r;
    f->pos = 0;
    return 0;
}

int fgetc(FILE *f)
{
    if (!f->reading || f->pos >= f->len)
        if (fill(f) == EOF)
            return EOF;
    return (unsigned char)f->buf[f->pos++];
}

int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

size_t fread(void *p, size_t sz, size_t n, FILE *f)
{
    size_t total = sz * n, got = 0;
    char *d = p;
    while (got < total) {
        int c = fgetc(f);
        if (c == EOF)
            break;
        d[got++] = (char)c;
    }
    return sz ? got / sz : 0;
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF)
            break;
        s[i++] = (char)c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return 0;
    s[i] = 0;
    return s;
}

FILE *fopen(const char *path, const char *mode)
{
    int flags = 0, rw = 0;
    if (mode[0] == 'r') { flags = O_RDONLY; rw = 1; }
    else if (mode[0] == 'w') { flags = O_WRONLY | O_CREAT | O_TRUNC; rw = 2; }
    else if (mode[0] == 'a') { flags = O_WRONLY | O_CREAT | O_APPEND; rw = 2; }
    else { errno = EINVAL; return 0; }
    if (strchr(mode, '+')) {
        flags = (flags & ~O_ACCMODE) | O_RDWR;
        rw = 3;
    }
    int fd = open(path, flags);
    if (fd < 0)
        return 0;
    FILE *f = calloc(1, sizeof *f);
    char *b = malloc(BUFSIZ);
    if (!f || !b) {
        free(f);
        free(b);
        close(fd);
        errno = ENOMEM;
        return 0;
    }
    f->fd = fd;
    f->flags = rw;
    f->buf = b;
    f->cap = BUFSIZ;
    return f;
}

int fclose(FILE *f)
{
    fflush(f);
    int r = close(f->fd);
    if (f != stdin && f != stdout && f != stderr) {
        free(f->buf);
        free(f);
    }
    return r;
}

int fseek(FILE *f, long off, int whence)
{
    fflush(f);
    if (f->reading) {
        if (whence == SEEK_CUR)
            off -= (long)(f->len - f->pos);
        f->len = f->pos = 0;
    }
    f->eof = 0;
    return lseek(f->fd, off, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f)
{
    long p = lseek(f->fd, 0, SEEK_CUR);
    if (p < 0)
        return -1;
    return f->reading ? p - (long)(f->len - f->pos) : p + (long)f->len;
}

int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }
int fileno(FILE *f) { return f->fd; }

void perror(const char *s)
{
    if (s && *s)
        fprintf(stderr, "%s: %s\n", s, strerror(errno));
    else
        fprintf(stderr, "%s\n", strerror(errno));
}

int remove(const char *path)
{
    return unlink(path);
}

/* ---- formatting engine ---- */
struct sink {
    char *buf;
    size_t cap, pos;
    FILE *f;
};

static void emit(struct sink *s, const char *p, size_t n)
{
    if (s->f) {
        fwrite(p, 1, n, s->f);
    } else {
        for (size_t i = 0; i < n; i++, s->pos++)
            if (s->pos + 1 < s->cap)
                s->buf[s->pos] = p[i];
        return;
    }
    s->pos += n;
}

static void pad(struct sink *s, char c, int n)
{
    while (n-- > 0)
        emit(s, &c, 1);
}

/* format a finite non-negative double; returns length written into out */
static int fmt_fixed(char *out, long double v, int prec)
{
    /* integer part */
    long double ip = 1;
    int digits = 1;
    long double r = 0.5L;
    for (int i = 0; i < prec; i++)
        r /= 10;
    v += r;                                 /* round half up */
    while (ip * 10 <= v && digits < 330) {
        ip *= 10;
        digits++;
    }
    int n = 0;
    for (int i = 0; i < digits; i++) {
        int d = (int)(v / ip);
        if (d > 9) d = 9;
        if (d < 0) d = 0;
        out[n++] = (char)('0' + d);
        v -= d * ip;
        ip /= 10;
    }
    if (prec > 0) {
        out[n++] = '.';
        for (int i = 0; i < prec; i++) {
            v *= 10;
            int d = (int)v;
            if (d > 9) d = 9;
            if (d < 0) d = 0;
            out[n++] = (char)('0' + d);
            v -= d;
        }
    }
    return n;
}

static int fmt_exp(char *out, long double v, int prec, char e)
{
    int ex = 0;
    if (v != 0) {
        while (v >= 10) { v /= 10; ex++; }
        while (v < 1) { v *= 10; ex--; }
    }
    char tmp[64];
    int n = fmt_fixed(tmp, v, prec);
    if (tmp[0] == '1' && tmp[1] == '0') {   /* rounding overflow 9.99 -> 10.0 */
        v /= 10;
        ex++;
        n = fmt_fixed(tmp, v, prec);
    }
    memcpy(out, tmp, (size_t)n);
    out[n++] = e;
    out[n++] = ex < 0 ? '-' : '+';
    if (ex < 0) ex = -ex;
    if (ex >= 100) out[n++] = (char)('0' + ex / 100);
    out[n++] = (char)('0' + ex / 10 % 10);
    out[n++] = (char)('0' + ex % 10);
    return n;
}

static int fmt_double(char *out, double d, char conv, int prec, int alt)
{
    if (d != d)
        return (int)(strcpy(out, conv >= 'a' ? "nan" : "NAN"), 3);
    if (d == __builtin_inf() || d == -__builtin_inf())
        return (int)(strcpy(out, conv >= 'a' ? "inf" : "INF"), 3);
    long double v = d < 0 ? -(long double)d : d;
    if (prec < 0)
        prec = 6;
    if (prec > 40)
        prec = 40;
    char c = conv | 32;
    if (c == 'f')
        return fmt_fixed(out, v, prec);
    if (c == 'e')
        return fmt_exp(out, v, prec, conv == 'E' ? 'E' : 'e');
    /* %g */
    if (prec == 0)
        prec = 1;
    int ex = 0;
    long double t = v;
    if (t != 0) {
        while (t >= 10) { t /= 10; ex++; }
        while (t < 1) { t *= 10; ex--; }
    }
    int n;
    if (ex < -4 || ex >= prec)
        n = fmt_exp(out, v, prec - 1, conv == 'G' ? 'E' : 'e');
    else
        n = fmt_fixed(out, v, prec - 1 - ex);
    if (!alt) {
        /* strip trailing zeros of the fraction */
        char *ep = memchr(out, conv == 'G' ? 'E' : 'e', (size_t)n);
        int mant = ep ? (int)(ep - out) : n;
        if (memchr(out, '.', (size_t)mant)) {
            int m = mant;
            while (m > 0 && out[m - 1] == '0')
                m--;
            if (m > 0 && out[m - 1] == '.')
                m--;
            memmove(out + m, out + mant, (size_t)(n - mant));
            n -= mant - m;
        }
    }
    return n;
}

static int vformat(struct sink *s, const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            const char *e = fmt;
            while (e[1] && e[1] != '%')
                e++;
            emit(s, fmt, (size_t)(e - fmt + 1));
            fmt = e;
            continue;
        }
        fmt++;
        int left = 0, plus = 0, space = 0, zero = 0, alt = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '+') plus = 1;
            else if (*fmt == ' ') space = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt == '#') alt = 1;
            else break;
        }
        int width = 0, prec = -1;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) { left = 1; width = -width; }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }
        int lng = 0;
        for (;; fmt++) {
            if (*fmt == 'l') lng++;
            else if (*fmt == 'z' || *fmt == 't' || *fmt == 'h') ;
            else if (*fmt == 'j' || *fmt == 'q' || *fmt == 'L') lng = 2;
            else break;
        }
        char tmp[400];
        const char *str = tmp;
        int len = 0;
        char sign = 0;
        int numeric = 0;
        switch (*fmt) {
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            len = 1;
            break;
        case 's':
            str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            len = (int)(prec >= 0 ? strnlen(str, (size_t)prec) : strlen(str));
            break;
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': case 'p': {
            unsigned long long v;
            int base = 10, upper = *fmt == 'X';
            if (*fmt == 'p') {
                v = (uintptr_t)va_arg(ap, void *);
                base = 16;
                alt = 1;
            } else if (*fmt == 'd' || *fmt == 'i') {
                long long sv = lng >= 2 ? va_arg(ap, long long) : va_arg(ap, int);
                if (sv < 0) { sign = '-'; v = (unsigned long long)-sv; }
                else { v = (unsigned long long)sv; sign = plus ? '+' : space ? ' ' : 0; }
            } else {
                v = lng >= 2 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned);
                base = *fmt == 'o' ? 8 : *fmt == 'u' ? 10 : 16;
            }
            char r[32];
            int rl = 0;
            while (v) {
                unsigned dg = (unsigned)(v % (unsigned)base);
                r[rl++] = (char)(dg < 10 ? '0' + dg : (upper ? 'A' : 'a') + dg - 10);
                v /= (unsigned)base;
            }
            if (prec < 0 && rl == 0)
                r[rl++] = '0';
            while (rl < prec)
                r[rl++] = '0';
            if (alt && base == 16) {
                tmp[len++] = '0';
                tmp[len++] = upper ? 'X' : 'x';
            } else if (alt && base == 8 && (rl == 0 || r[rl - 1] != '0')) {
                r[rl++] = '0';
            }
            while (rl)
                tmp[len++] = r[--rl];
            numeric = prec < 0;
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double d = lng >= 2 ? (double)va_arg(ap, long double) : va_arg(ap, double);
            if (d < 0 || (d == 0 && __builtin_signbit(d)))
                sign = '-';
            else
                sign = plus ? '+' : space ? ' ' : 0;
            len = fmt_double(tmp, d, *fmt, prec, alt);
            numeric = 1;
            break;
        }
        case 'n':
            *va_arg(ap, int *) = (int)s->pos;
            continue;
        case '%':
            tmp[0] = '%';
            len = 1;
            break;
        default:
            tmp[0] = '%';
            tmp[1] = *fmt;
            len = 2;
            if (!*fmt) { len = 1; fmt--; }
        }
        int total = len + (sign ? 1 : 0);
        int padn = width > total ? width - total : 0;
        if (!left && !(zero && numeric))
            pad(s, ' ', padn);
        if (sign)
            emit(s, &sign, 1);
        if (!left && zero && numeric)
            pad(s, '0', padn);
        emit(s, str, (size_t)len);
        if (left)
            pad(s, ' ', padn);
    }
    return (int)s->pos;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    struct sink s = { buf, n, 0, 0 };
    vformat(&s, fmt, ap);
    if (n)
        buf[s.pos < n ? s.pos : n - 1] = 0;
    return (int)s.pos;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    return vsnprintf(buf, (size_t)-1 >> 1, fmt, ap);
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    struct sink s = { 0, 0, 0, f };
    return vformat(&s, fmt, ap);
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    va_end(ap);
    return r;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}
