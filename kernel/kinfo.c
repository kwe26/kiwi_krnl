/* SYS_kinfo / SYS_peek / SYS_portio: kernel internals for user space. */
#include "kernel.h"

extern char kernel_start[], kernel_end[];

static int fill_proc(int pid, struct kiwi_procdetail *d)
{
    struct proc *p = proc_by_pid(pid ? pid : current->pid);
    if (!p)
        return -ESRCH;
    memset(d, 0, sizeof *d);
    d->pid = p->pid;
    d->ppid = p->ppid;
    d->pgid = p->pgid;
    d->state = p->state;
    strlcpy(d->name, p->name, sizeof d->name);
    strlcpy(d->cwd, p->cwd, sizeof d->cwd);
    d->kstack = p->kstack;
    d->kesp = p->kesp;
    d->pd = p->pd;
    d->heap_start = p->heap_start;
    d->brk = p->brk;
    d->mmap_next = p->mmap_next;
    d->resident = p->resident;
    d->compressed = p->compressed;
    d->ticks = p->ticks;
    d->killed = p->killed;
    d->iopl = p->iopl;
    d->nvma = p->nvma;
    memcpy(d->vma, p->vma, sizeof d->vma);
    for (int i = 0; i < NFD && i < 16; i++)
        d->fd_type[i] = p->fds[i] ? (uint8_t)p->fds[i]->type : 0;
    return 0;
}

static int pagemap(int pid, struct kiwi_pte *u, uint32_t max)
{
    struct proc *p = proc_by_pid(pid ? pid : current->pid);
    if (!p || !p->pd)
        return -ESRCH;
    uint32_t n = 0;
    for (uint32_t va = USER_BASE; va < USER_END && n < max; ) {
        if (!(((uint32_t *)p->pd)[va >> 22] & PTE_P)) {
            va = (va & ~0x3FFFFFu) + 0x400000u;
            continue;
        }
        uint32_t pte = *vmm_pte(p->pd, va, 0);
        if (pte) {
            struct kiwi_pte e = { va, pte };
            if (copy_to_user(u + n, &e, sizeof e) < 0)
                return -EFAULT;
            n++;
        }
        va += PAGE_SIZE;
    }
    return (int)n;
}

int sys_kinfo(int what, int arg, uint8_t *ubuf, uint32_t len)
{
    if (!user_ok(ubuf, len))
        return -EFAULT;
    switch (what) {
    case KINFO_SYSTEM: {
        struct kiwi_sysinfo si;
        memset(&si, 0, sizeof si);
        cpu_sysinfo(&si);
        si.kernel_start = (uint32_t)kernel_start;
        si.kernel_end = (uint32_t)kernel_end;
        si.heap_start = kheap_start;
        si.heap_size = kheap_size;
        si.total_frames = pmm_total_frames;
        si.free_frames = pmm_free_frames;
        si.max_frame = pmm_max_frame;
        si.ticks = ticks;
        si.hz = HZ;
        si.boot_epoch = boot_epoch;
        for (int i = 0; i < NPROC; i++)
            si.nprocs += procs[i].state != P_UNUSED;
        si.syscalls = irq_counts[KINFO_NVEC - 1];
        strlcpy(si.cmdline, kernel_cmdline, sizeof si.cmdline);
        if (len < sizeof si)
            return -EINVAL;
        return copy_to_user(ubuf, &si, sizeof si) < 0 ? -EFAULT : (int)sizeof si;
    }
    case KINFO_GDT:
    case KINFO_IDT: {
        uint32_t n;
        const void *t = cpu_table(what == KINFO_IDT, &n);
        if (n > len) n = len;
        return copy_to_user(ubuf, t, n) < 0 ? -EFAULT : (int)n;
    }
    case KINFO_IRQ: {
        uint32_t n = sizeof irq_counts < len ? sizeof irq_counts : len;
        return copy_to_user(ubuf, irq_counts, n) < 0 ? -EFAULT : (int)n;
    }
    case KINFO_MEMMAP: {
        uint32_t n = (uint32_t)boot_mmap_n * sizeof(struct kiwi_memrange);
        if (n > len) n = len;
        return copy_to_user(ubuf, boot_mmap, n) < 0 ? -EFAULT : (int)n;
    }
    case KINFO_PAGEMAP:
        return pagemap(arg, (struct kiwi_pte *)ubuf, len / sizeof(struct kiwi_pte));
    case KINFO_DMESG: {
        char buf[512];
        uint32_t got = 0, c;
        while (got < len && (c = klog_read(buf, got, len - got < sizeof buf ? len - got : sizeof buf))) {
            if (copy_to_user(ubuf + got, buf, c) < 0)
                return -EFAULT;
            got += c;
        }
        return (int)got;
    }
    case KINFO_PCI: {
        uint32_t n = (uint32_t)pci_ndev * sizeof(struct kiwi_pcidev);
        if (n > len) n = len - len % sizeof(struct kiwi_pcidev);
        return copy_to_user(ubuf, pci_devs, n) < 0 ? -EFAULT : (int)n;
    }
    case KINFO_PROC: {
        struct kiwi_procdetail d;
        int r = fill_proc(arg, &d);
        if (r < 0)
            return r;
        if (len < sizeof d)
            return -EINVAL;
        return copy_to_user(ubuf, &d, sizeof d) < 0 ? -EFAULT : (int)sizeof d;
    }
    case KINFO_ZRAM: {
        struct kiwi_zraminfo z;
        zram_info(&z);
        if (len < sizeof z)
            return -EINVAL;
        return copy_to_user(ubuf, &z, sizeof z) < 0 ? -EFAULT : (int)sizeof z;
    }
    case KINFO_FONT: {
        uint32_t n = len < sizeof vga_font ? len : sizeof vga_font;
        return copy_to_user(ubuf, vga_font, n) < 0 ? -EFAULT : (int)n;
    }
    }
    return -EINVAL;
}

int sys_peek(uint32_t phys, uint8_t *ubuf, uint32_t len)
{
    if (len > 65536 || phys + len < phys || phys + len > pmm_max_frame * PAGE_SIZE)
        return -EFAULT;
    return copy_to_user(ubuf, (const void *)phys, len) < 0 ? -EFAULT : (int)len;
}

int sys_portio(int op, uint32_t port, uint32_t val)
{
    uint16_t p = (uint16_t)port;
    switch (op) {
    case PIO_INB:  return inb(p);
    case PIO_INW:  return inw(p);
    case PIO_INL:  return (int)inl(p);
    case PIO_OUTB: outb(p, (uint8_t)val); return 0;
    case PIO_OUTW: outw(p, (uint16_t)val); return 0;
    case PIO_OUTL: outl(p, val); return 0;
    }
    return -EINVAL;
}
