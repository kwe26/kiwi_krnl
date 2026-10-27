/* string.h */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

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
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp <= sp || dp >= sp + n)
        return memcpy(d, s, n);
    dp += n;
    sp += n;
    while (n--)
        *--dp = *--sp;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    void *ret = d;
    unsigned v = (unsigned char)c * 0x01010101u;
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
    const unsigned char *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] - y[i];
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (unsigned char)c)
            return (void *)(p + i);
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            break;
    }
    return 0;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower(*a) == tolower(*b))
        a++, b++;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y)
            return x - y;
        if (!x)
            break;
    }
    return 0;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
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

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *e = d + strlen(d);
    size_t i = 0;
    for (; i < n && s[i]; i++)
        e[i] = s[i];
    e[i] = 0;
    return d;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c ? 0 : (char *)s;
}

char *strrchr(const char *s, int c)
{
    const char *r = 0;
    do {
        if (*s == (char)c)
            r = s;
    } while (*s++);
    return (char *)r;
}

char *strstr(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl)
        return (char *)h;
    for (; *h; h++)
        if (*h == *n && strncmp(h, n, nl) == 0)
            return (char *)h;
    return 0;
}

char *strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

char *strndup(const char *s, size_t n)
{
    size_t l = strnlen(s, n);
    char *d = malloc(l + 1);
    if (d) {
        memcpy(d, s, l);
        d[l] = 0;
    }
    return d;
}

size_t strspn(const char *s, const char *a)
{
    size_t i = 0;
    while (s[i] && strchr(a, s[i]))
        i++;
    return i;
}

size_t strcspn(const char *s, const char *r)
{
    size_t i = 0;
    while (s[i] && !strchr(r, s[i]))
        i++;
    return i;
}

char *strtok(char *s, const char *delim)
{
    static char *save;
    if (s)
        save = s;
    if (!save)
        return 0;
    save += strspn(save, delim);
    if (!*save)
        return save = 0;
    char *tok = save;
    save += strcspn(save, delim);
    if (*save)
        *save++ = 0;
    else
        save = 0;
    return tok;
}

char *strerror(int e)
{
    switch (e) {
    case 0:       return "Success";
    case EPERM:   return "Operation not permitted";
    case ENOENT:  return "No such file or directory";
    case ESRCH:   return "No such process";
    case EINTR:   return "Interrupted";
    case EIO:     return "I/O error";
    case E2BIG:   return "Argument list too long";
    case ENOEXEC: return "Exec format error";
    case EBADF:   return "Bad file descriptor";
    case ECHILD:  return "No child processes";
    case EAGAIN:  return "Try again";
    case ENOMEM:  return "Out of memory";
    case EACCES:  return "Permission denied";
    case EFAULT:  return "Bad address";
    case EBUSY:   return "Device or resource busy";
    case EEXIST:  return "File exists";
    case ENOTDIR: return "Not a directory";
    case EISDIR:  return "Is a directory";
    case EINVAL:  return "Invalid argument";
    case EMFILE:  return "Too many open files";
    case ENOTTY:  return "Not a terminal";
    case ENOSPC:  return "No space left";
    case ESPIPE:  return "Illegal seek";
    case EPIPE:   return "Broken pipe";
    case ERANGE:  return "Result out of range";
    case ENAMETOOLONG: return "File name too long";
    case ENOSYS:  return "Function not implemented";
    case ENOTEMPTY: return "Directory not empty";
    }
    return "Unknown error";
}
