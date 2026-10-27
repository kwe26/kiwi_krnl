/*
 * mmap/munmap: anonymous memory (demand-zero, compressible like any other
 * user page) and device memory such as the framebuffer (PTE_PHYS: never
 * compressed, never freed).  Each process has a few VMAs in [MMAP_BASE,
 * MMAP_END).
 */
#include "kernel.h"

void vma_reset(struct proc *p)
{
    p->nvma = 0;
    p->mmap_next = MMAP_BASE;
}

int vma_anon(struct proc *p, uint32_t addr)
{
    for (uint32_t i = 0; i < p->nvma; i++)
        if (p->vma[i].type == VMA_ANON && addr >= p->vma[i].start && addr < p->vma[i].end)
            return 1;
    return 0;
}

int sys_mmap(const struct kiwi_mmap *u)
{
    struct kiwi_mmap m;
    struct proc *p = current;
    if (copy_from_user(&m, u, sizeof m) < 0)
        return -EFAULT;
    uint32_t len = ALIGN_UP(m.len, PAGE_SIZE);
    if (!len || p->nvma >= 8 || len > MMAP_END - p->mmap_next)
        return -ENOMEM;
    uint32_t addr = p->mmap_next;

    if (m.flags & MAP_ANON) {
        p->vma[p->nvma++] = (struct kiwi_vma){ addr, addr + len, VMA_ANON };
    } else {
        if (m.fd < 0 || m.fd >= NFD || !p->fds[m.fd])
            return -EBADF;
        struct file *f = p->fds[m.fd];
        const struct devops *d = f->type == F_DEV ? dev_ops(f->dev) : 0;
        if (!d || !d->mmap)
            return -ENODEV;
        for (uint32_t o = 0; o < len; o += PAGE_SIZE) {
            uint32_t phys = d->mmap(f, m.off + o);
            uint32_t *pte = phys ? vmm_pte(p->pd, addr + o, 1) : 0;
            if (!pte) {
                vmm_unmap_range(p, p->pd, addr, addr + o);
                return phys ? -ENOMEM : -EINVAL;
            }
            *pte = (phys & PAGE_MASK) | PTE_P | PTE_U | PTE_PHYS |
                   ((m.prot & PROT_WRITE) ? PTE_W : 0);
        }
        p->vma[p->nvma++] = (struct kiwi_vma){ addr, addr + len, VMA_PHYS };
    }
    p->mmap_next = addr + len + PAGE_SIZE;      /* guard page */
    return (int)addr;
}

int sys_munmap(uint32_t addr, uint32_t len)
{
    struct proc *p = current;
    len = ALIGN_UP(len, PAGE_SIZE);
    for (uint32_t i = 0; i < p->nvma; i++) {
        struct kiwi_vma *v = &p->vma[i];
        if (addr >= v->start && addr < v->end) {
            uint32_t end = addr + len > v->end ? v->end : addr + len;
            vmm_unmap_range(p, p->pd, addr & PAGE_MASK, end);
            if (addr <= v->start && end >= v->end)
                p->vma[i] = p->vma[--p->nvma];
            return 0;
        }
    }
    return -EINVAL;
}
