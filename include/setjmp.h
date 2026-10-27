#ifndef _SETJMP_H
#define _SETJMP_H
typedef unsigned int jmp_buf[6];   /* ebx esi edi ebp esp eip */
int  setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#endif
