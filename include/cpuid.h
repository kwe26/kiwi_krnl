#ifndef _CPUID_H
#define _CPUID_H
static inline void __cpuid_count(unsigned leaf, unsigned sub, unsigned *a, unsigned *b, unsigned *c, unsigned *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}
static inline int __get_cpuid(unsigned leaf, unsigned *a, unsigned *b, unsigned *c, unsigned *d)
{
    __cpuid_count(leaf & 0x80000000u, 0, a, b, c, d);
    if (*a < leaf)
        return 0;
    __cpuid_count(leaf, 0, a, b, c, d);
    return 1;
}
#endif
