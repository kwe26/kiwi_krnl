/*
 * Kernel introspection: SYS_kinfo(what, arg, buf, len) copies kernel
 * internals into buf and returns the number of bytes (or records) written.
 */
#ifndef _KIWI_KINFO_H
#define _KIWI_KINFO_H

#include <stdint.h>

#define KINFO_SYSTEM   1   /* struct kiwi_sysinfo */
#define KINFO_GDT      2   /* raw descriptors, 8 bytes each */
#define KINFO_IDT      3   /* raw gates, 8 bytes each (256) */
#define KINFO_IRQ      4   /* uint32_t[KINFO_NVEC] interrupt counters */
#define KINFO_MEMMAP   5   /* struct kiwi_memrange[] from the boot loader */
#define KINFO_PAGEMAP  6   /* arg = pid: struct kiwi_pte[] of user space */
#define KINFO_DMESG    7   /* kernel log text */
#define KINFO_PCI      8   /* struct kiwi_pcidev[] */
#define KINFO_PROC     9   /* arg = pid: struct kiwi_procdetail */
#define KINFO_ZRAM     10  /* struct kiwi_zraminfo */
#define KINFO_FONT     11  /* 4096 bytes: VGA BIOS 8x16 font, 16 bytes/glyph */

#define KINFO_NVEC     49  /* vectors 0..47, then int 0x80 */

struct kiwi_sysinfo {
    uint32_t cr0, cr2, cr3, cr4, eflags;
    uint32_t gdt_base, gdt_limit, idt_base, idt_limit;
    uint32_t tss_esp0;
    uint32_t kernel_start, kernel_end;
    uint32_t heap_start, heap_size;
    uint32_t total_frames, free_frames, max_frame;
    uint32_t ticks, hz, boot_epoch;
    uint32_t nprocs, syscalls;
    char     cmdline[128];
};

struct kiwi_memrange {
    uint64_t addr, len;
    uint32_t type;               /* 1 = usable RAM */
    uint32_t pad;
};

/* PTE flag bits as seen in struct kiwi_pte */
#define KPTE_PRESENT   0x001
#define KPTE_WRITE     0x002
#define KPTE_USER      0x004
#define KPTE_ACCESSED  0x020
#define KPTE_DIRTY     0x040
#define KPTE_ZRAM      0x200     /* compressed: pte >> 12 is the zram slot */
#define KPTE_PHYS      0x400     /* device memory (e.g. framebuffer) */

struct kiwi_pte {
    uint32_t va;
    uint32_t pte;
};

struct kiwi_pcidev {
    uint8_t  bus, dev, fn, irq;
    uint16_t vendor, device;
    uint8_t  cls, subclass, progif, rev;
    uint32_t bar[6];
};

struct kiwi_vma {
    uint32_t start, end;
    uint32_t type;               /* 1 anonymous, 2 device memory */
};

struct kiwi_procdetail {
    int32_t  pid, ppid, pgid, state;
    char     name[32];
    char     cwd[64];
    uint32_t kstack, kesp, pd;
    uint32_t heap_start, brk, mmap_next;
    uint32_t resident, compressed, ticks;
    int32_t  killed, iopl;
    uint32_t nvma;
    struct kiwi_vma vma[8];
    uint8_t  fd_type[16];        /* 0 closed, 1 console, 2 file, 3 dir, 4 pipe r, 5 pipe w, 6 device */
};

struct kiwi_zraminfo {
    uint32_t slots_total;        /* slot table capacity */
    uint32_t slots_used;
    uint32_t by_type[5];         /* free same lz raw lzh (unique objects) */
    uint32_t size_hist[17];      /* unique compressed sizes, 256-byte buckets */
    uint32_t max_refs;           /* most-shared object */
    uint32_t shared_objects;     /* objects referenced more than once */
    uint32_t budget, resident, clock_hand, idle_hand;
};

#endif
