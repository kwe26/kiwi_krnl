#ifndef _ASSERT_H
#define _ASSERT_H
void __kiwi_assert_fail(const char *expr, const char *file, int line, const char *fn)
    __attribute__((noreturn));
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __kiwi_assert_fail(#e, __FILE__, __LINE__, __func__))
#endif
#ifndef __cplusplus
#define static_assert _Static_assert
#endif
#endif
