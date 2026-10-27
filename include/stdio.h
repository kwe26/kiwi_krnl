#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
#define BUFSIZ 1024
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
typedef struct _kiwi_file FILE;
extern FILE *stdin, *stdout, *stderr;
int    printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int    fprintf(FILE *f, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int    sprintf(char *buf, const char *fmt, ...);
int    snprintf(char *buf, size_t n, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int    vprintf(const char *fmt, va_list ap);
int    vfprintf(FILE *f, const char *fmt, va_list ap);
int    vsprintf(char *buf, const char *fmt, va_list ap);
int    vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int    putchar(int c);
int    puts(const char *s);
int    fputc(int c, FILE *f);
int    putc(int c, FILE *f);
int    fputs(const char *s, FILE *f);
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f);
size_t fread(void *p, size_t sz, size_t n, FILE *f);
int    fgetc(FILE *f);
int    getc(FILE *f);
int    getchar(void);
char  *fgets(char *s, int n, FILE *f);
int    fflush(FILE *f);
FILE  *fopen(const char *path, const char *mode);
int    fclose(FILE *f);
int    fseek(FILE *f, long off, int whence);
long   ftell(FILE *f);
int    feof(FILE *f);
int    ferror(FILE *f);
int    fileno(FILE *f);
void   perror(const char *s);
int    remove(const char *path);
int    rename(const char *a, const char *b);
#endif
