/*
 * Direct x86 port I/O from user space, Linux style: call iopl(3) once,
 * then use the inline in/out helpers.  (Without iopl they fault.)
 */
#ifndef _SYS_IO_H
#define _SYS_IO_H
#include <stdint.h>
int iopl(int level);
/* the same operations through the kernel, no iopl needed */
uint32_t port_in(int size, uint16_t port);
void     port_out(int size, uint16_t port, uint32_t value);
static inline uint8_t  inb(uint16_t p) { uint8_t v;  __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outb(uint8_t v, uint16_t p)  { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outw(uint16_t v, uint16_t p) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outl(uint32_t v, uint16_t p) { __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"(p)); }
#endif
