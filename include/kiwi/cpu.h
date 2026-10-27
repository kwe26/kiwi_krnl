/* Kiwi: unprivileged CPU helpers for user programs. */
#ifndef _KIWI_CPU_H
#define _KIWI_CPU_H
#include <stdint.h>
#include <cpuid.h>
static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static inline uint32_t read_eflags(void)
{
    uint32_t f;
    __asm__ volatile("pushfl; popl %0" : "=r"(f));
    return f;
}
/* 12-byte vendor string (+ NUL) and 48-byte brand string (+ NUL) */
static inline void cpu_vendor(char out[13])
{
    unsigned a, b, c, d;
    __cpuid_count(0, 0, &a, &b, &c, &d);
    ((unsigned *)out)[0] = b; ((unsigned *)out)[1] = d; ((unsigned *)out)[2] = c;
    out[12] = 0;
}
static inline void cpu_brand(char out[49])
{
    unsigned *w = (unsigned *)out;
    for (unsigned i = 0; i < 3; i++)
        __cpuid_count(0x80000002u + i, 0, &w[i * 4], &w[i * 4 + 1], &w[i * 4 + 2], &w[i * 4 + 3]);
    out[48] = 0;
}
#endif
