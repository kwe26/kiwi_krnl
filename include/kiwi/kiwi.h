/* Kiwi-specific system interfaces (memory compression, processes, tty). */
#ifndef _KIWI_KIWI_H
#define _KIWI_KIWI_H
#include <sys/types.h>
#include <kiwi/abi.h>
int      kz_stat(struct kiwi_kzstat *st);
int      kz_ctl(int op, int arg);
int      kiwi_ps(struct kiwi_procinfo *buf, int max);
pid_t    kiwi_spawn(const struct kiwi_spawn *sp);
int      kiwi_poweroff(void);
uint32_t kiwi_uptime_ms(void);
int      kiwi_sleep_ms(unsigned ms);
int      tty_set_raw(int fd, int raw);
int      tty_set_fg(int fd, pid_t pgid);
int      tty_get_size(int fd, int *rows, int *cols);
long     kiwi_syscall(long n, long a, long b, long c, long d, long e);

/* kernel introspection (see kiwi/kinfo.h) */
int      kinfo(int what, int arg, void *buf, size_t len);
/* read physical memory (RAM, BIOS area, VGA memory...) */
int      kiwi_peek(uint32_t phys, void *buf, size_t len);
#endif
