#ifndef _STDDEF_H
#define _STDDEF_H
typedef unsigned int size_t;
typedef int          ssize_t;
typedef int          ptrdiff_t;
#ifndef __cplusplus
typedef int          wchar_t;
#endif
typedef struct { long long __ll; long double __ld; } max_align_t;
#define NULL ((void *)0)
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
