#ifndef KIWI_KERNEL_H
#define KIWI_KERNEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <kiwi/abi.h>

#define PAGE_SIZE   4096u
#define PAGE_MASK   (~(PAGE_SIZE - 1))
#define ALIGN_UP(x, a)   (((x) + (a) - 1) & ~((a) - 1))

/* user address space */
#define USER_BASE        0x40000000u
#define USER_STACK_TOP   0xC0000000u
#define USER_STACK_MAX   (8u << 20)
#define USER_END         USER_STACK_TOP

#define KCODE_SEL 0x08
#define KDATA_SEL 0x10
#define UCODE_SEL 0x1B
#define UDATA_SEL 0x23
#define TSS_SEL   0x28

/* trap frame pushed by boot.S */
struct regs {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    uint32_t int_no, err;
    uint32_t eip, cs, eflags, useresp, ss;
};

/* ---- port I/O and cpu helpers ---- */
static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline void outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1" :: "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1" :: "a"(v), "Nd"(p)); }
static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }
static inline uint32_t read_cr2(void) { uint32_t v; __asm__ volatile("mov %%cr2,%0" : "=r"(v)); return v; }
static inline uint32_t read_cr3(void) { uint32_t v; __asm__ volatile("mov %%cr3,%0" : "=r"(v)); return v; }
static inline void write_cr3(uint32_t v) { __asm__ volatile("mov %0,%%cr3" :: "r"(v) : "memory"); }
static inline void invlpg(uint32_t va) { __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory"); }

/* ---- klib.c ---- */
void  *memcpy(void *d, const void *s, size_t n);
void  *memmove(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strchr(const char *s, int c);
size_t strlcpy(char *d, const char *s, size_t n);
int    ksnprintf(char *buf, size_t n, const char *fmt, ...);
int    kvsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
void   kprintf(const char *fmt, ...);
uint32_t klog_read(char *dst, uint32_t off, uint32_t n);
void   panic(const char *fmt, ...) __attribute__((noreturn));

/* ---- console.c ---- */
void console_init(void);
void console_write(const char *s, size_t n);
void serial_init(void);
void tty_input(uint8_t c);              /* from keyboard/serial IRQ */
int  tty_read(char *buf, size_t n);     /* may block */
int  tty_ioctl(int req, uint32_t arg);
void keyboard_irq(void);
void serial_irq(void);
void console_shadow(int on);            /* graphics mode owns VGA memory */

/* ---- cpu.c ---- */
void gdt_init(void);
void idt_init(void);
void tss_set_kstack(uint32_t esp0);
void pic_eoi(int irq);
void pit_init(int hz);
extern volatile uint32_t ticks;
#define HZ 100
extern uint32_t irq_counts[KINFO_NVEC];
void cpu_sysinfo(struct kiwi_sysinfo *si);
const void *cpu_table(int idt, uint32_t *len);

/* ---- pmm.c ---- */
struct multiboot_info;
void     pmm_init(struct multiboot_info *mbi, uint32_t reserve_end);
uint32_t pmm_alloc(void);           /* returns physical address or 0 */
void     pmm_free(uint32_t pa);
void     pmm_reserve(uint32_t start, uint32_t end);
void     pmm_release(uint32_t start, uint32_t end);
extern uint32_t pmm_total_frames, pmm_free_frames, pmm_max_frame;
extern struct kiwi_memrange boot_mmap[32];
extern int boot_mmap_n;

/* ---- heap.c ---- */
void   heap_init(uint32_t start, uint32_t size);
void  *kmalloc(size_t n);
void  *kzalloc(size_t n);
void  *krealloc(void *p, size_t n);
void   kfree(void *p);
char  *kstrdup(const char *s);
void   heap_stats(uint32_t *total, uint32_t *used);
extern uint32_t kheap_start, kheap_size;

/* ---- vmm.c ---- */
#define PTE_P    0x001
#define PTE_W    0x002
#define PTE_U    0x004
#define PTE_A    0x020
#define PTE_D    0x040
#define PTE_PS   0x080
#define PTE_Z    0x200      /* avail bit: entry holds a zram slot, not a frame */
#define PTE_PHYS 0x400      /* avail bit: device memory, not a tracked frame */

struct proc;
void      vmm_init(void);
uint32_t  vmm_new_pd(void);
void      vmm_free_pd(struct proc *owner, uint32_t pd);
uint32_t *vmm_pte(uint32_t pd, uint32_t va, int create);
uint8_t  *vmm_user_page(struct proc *owner, uint32_t pd, uint32_t va, int alloc);
int       vmm_copy_space(struct proc *dst, uint32_t dpd, uint32_t spd);
void      vmm_unmap_range(struct proc *owner, uint32_t pd, uint32_t start, uint32_t end);
void      page_fault(struct regs *r);
extern uint32_t kernel_pd;

/* ---- zram.c: compressed memory ---- */
void     zram_init(void);
uint32_t uframe_alloc(struct proc *owner, uint32_t pd, uint32_t va);
void     uframe_release(uint32_t pa);
int      zram_page_in(struct proc *owner, uint32_t pd, uint32_t va, uint32_t *pte);
void     zram_slot_free(uint32_t slot);
void     zram_slot_dup(uint32_t slot);
void     zram_idle_scan(void);
int      zram_compress_all(void);
void     zram_set_budget(uint32_t pages);
void     zram_stat(struct kiwi_kzstat *st);
void     zram_info(struct kiwi_zraminfo *z);
extern uint32_t zram_budget, zram_proactive;

/* ---- ramfs.c ---- */
struct inode;
void          ramfs_init(void);
void          ramfs_load_initrd(const uint8_t *img, uint32_t len);
struct inode *ramfs_lookup(const char *path, const char *cwd);
struct inode *ramfs_create(const char *path, const char *cwd, int dir, int *err);
int           ramfs_unlink(const char *path, const char *cwd);
int           ramfs_rename(const char *a, const char *b, const char *cwd);
int           ramfs_open_inode(struct inode *ino);
void          ramfs_close_inode(struct inode *ino);
int           ramfs_rw(struct inode *ino, uint32_t off, void *buf, uint32_t n, int write);
int           ramfs_truncate(struct inode *ino, uint32_t len);
uint8_t      *ramfs_read_all(struct inode *ino, uint32_t *len);
void          ramfs_stat(struct inode *ino, struct kiwi_stat *st);
int           ramfs_readdir(struct inode *dir, uint32_t idx, struct kiwi_dirent *de);
int           ramfs_isdir(struct inode *ino);
int           ramfs_path(struct inode *ino, char *buf, size_t n);
int           ramfs_mknod(const char *path, int devno);
int           ramfs_devno(struct inode *ino);
void          ramfs_totals(uint32_t *files, uint32_t *bytes, uint32_t *zbytes);
int           path_normalize(const char *path, const char *cwd, char *out, size_t n);

/* ---- proc.c ---- */
#define NPROC 64
#define NFD   16

enum { P_UNUSED, P_READY, P_BLOCKED, P_ZOMBIE };

struct file;

struct proc {
    int      pid, ppid, pgid;
    int      state;
    char     name[32];
    uint32_t kstack;        /* base of kernel stack allocation */
    uint32_t kesp;          /* saved kernel esp */
    uint32_t pd;            /* page directory (physical == virtual) */
    uint32_t heap_start, brk;
    struct file *fds[NFD];
    char     cwd[256];
    int      exit_status;
    void    *wait_chan;
    uint32_t wake_tick;
    int      killed;
    uint8_t *fpu_raw, *fpu;  /* fxsave area (16-byte aligned) */
    uint32_t resident, compressed;
    uint32_t ticks;
    struct regs *tf;
    int      iopl;
    uint32_t nvma, mmap_next;
    struct kiwi_vma vma[8];
};

extern struct proc procs[NPROC];
extern struct proc *current;
extern int need_resched;

void proc_init(void);
void schedule(void);
void sleep_on(void *chan);
void wakeup(void *chan);
void do_exit(int status) __attribute__((noreturn));
int  proc_fork(struct regs *r);
int  proc_execve(struct regs *r, const char *path, char *const *uargv);
int  proc_spawn(const struct kiwi_spawn *sp);
int  proc_waitpid(int pid, int *status, int options);
int  proc_kill(int pid, int sig);
void proc_kill_pgrp(int pgid);
int  proc_list(struct kiwi_procinfo *buf, int max);
int  proc_sbrk(int incr);
void proc_start_init(const char *path);
void idle_loop(void) __attribute__((noreturn));
struct proc *proc_by_pid(int pid);

/* ---- file.c (fd layer) ---- */
enum { F_CONSOLE = 1, F_FILE, F_DIR, F_PIPE_R, F_PIPE_W, F_DEV };
struct pipe;
struct file {
    int      type;
    int      refs;
    int      flags;
    uint32_t off;
    struct inode *ino;
    struct pipe  *pipe;
    int      dev;           /* F_DEV: device number */
    int      devdata;       /* per-open device state */
};
struct file *file_console(void);
struct file *file_dup(struct file *f);
void         file_close(struct file *f);
int          fd_alloc(struct proc *p, struct file *f);

/* ---- dev.c: character devices ---- */
enum { DEV_NULL, DEV_ZERO, DEV_KMSG, DEV_FB, DEV_INPUT, DEV_TTY, NDEV };
struct devops {
    const char *name;
    int      (*open)(struct file *f);
    void     (*close)(struct file *f);
    int      (*read)(struct file *f, uint8_t *ubuf, uint32_t n);
    int      (*write)(struct file *f, const uint8_t *ubuf, uint32_t n);
    int      (*ioctl)(struct file *f, int req, uint32_t arg);
    uint32_t (*mmap)(struct file *f, uint32_t off);    /* physical page or 0 */
};
const struct devops *dev_ops(int devno);
void dev_init(void);

/* ---- pci.c, fb.c, input.c ---- */
#define PCI_MAX 32
extern struct kiwi_pcidev pci_devs[PCI_MAX];
extern int pci_ndev;
void     pci_init(void);
uint32_t pci_read(int bus, int dev, int fn, int off);
struct kiwi_pcidev *pci_find(uint16_t vendor, uint16_t device);
extern uint8_t vga_font[4096];
void fb_init(void);
int  fb_set_mode(uint32_t w, uint32_t h, uint32_t bpp);
void fb_text_mode(void);
void fb_get_info(struct kiwi_fb_info *out);
extern int input_grabbed;
void input_init(void);
void input_grab(int on);
void input_key(uint16_t code, int down, uint32_t mods, uint32_t ch);
void mouse_irq(void);
int  input_read(uint8_t *ubuf, uint32_t n, int timeout_ms);

/* ---- mman.c, kinfo.c ---- */
#define VMA_ANON 1
#define VMA_PHYS 2
void vma_reset(struct proc *p);
int  vma_anon(struct proc *p, uint32_t addr);
int  sys_mmap(const struct kiwi_mmap *u);
int  sys_munmap(uint32_t addr, uint32_t len);
int  sys_kinfo(int what, int arg, uint8_t *ubuf, uint32_t len);
int  sys_peek(uint32_t phys, uint8_t *ubuf, uint32_t len);
int  sys_portio(int op, uint32_t port, uint32_t val);

/* ---- syscall.c ---- */
void syscall_dispatch(struct regs *r);
int  copy_from_user(void *dst, const void *usrc, size_t n);
int  copy_to_user(void *udst, const void *src, size_t n);
int  copy_str_from_user(char *dst, const char *usrc, size_t n);
int  user_ok(const void *p, size_t n);

/* ---- misc ---- */
extern uint32_t boot_epoch;
extern char kernel_cmdline[256];
void rtc_init(void);
void poweroff(void) __attribute__((noreturn));

#endif
