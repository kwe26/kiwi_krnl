/*
 * Paging.  Physical memory is identity mapped with 4MB pages in every
 * address space (supervisor only).  User space lives in [1GB, 3GB) and is
 * built from 4KB pages that may be resident, compressed in zram (PTE_Z),
 * or demand-zero (heap/stack, not yet touched).
 */
#include "kernel.h"

uint32_t kernel_pd;
uint32_t kframe_alloc(void);

#define PD(pd) ((uint32_t *)(pd))
#define KERNEL_PDES 256          /* PDEs below USER_BASE are shared */

void vmm_init(void)
{
    kernel_pd = kframe_alloc();
    uint32_t *pd = PD(kernel_pd);
    memset(pd, 0, PAGE_SIZE);
    uint32_t n4m = (pmm_max_frame * PAGE_SIZE + (4u << 20) - 1) >> 22;
    if (n4m > KERNEL_PDES)
        n4m = KERNEL_PDES;
    for (uint32_t i = 0; i < n4m; i++)
        pd[i] = (i << 22) | PTE_P | PTE_W | PTE_PS;

    uint32_t cr4;
    __asm__ volatile("mov %%cr4,%0" : "=r"(cr4));
    cr4 |= 0x10;                                /* PSE */
    __asm__ volatile("mov %0,%%cr4" :: "r"(cr4));
    write_cr3(kernel_pd);
    uint32_t cr0;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    cr0 |= 0x80010000;                          /* PG | WP */
    __asm__ volatile("mov %0,%%cr0" :: "r"(cr0));
}

uint32_t vmm_new_pd(void)
{
    uint32_t pd = kframe_alloc();
    if (!pd)
        return 0;
    memcpy(PD(pd), PD(kernel_pd), KERNEL_PDES * 4);
    memset(PD(pd) + KERNEL_PDES, 0, (1024 - KERNEL_PDES) * 4);
    return pd;
}

uint32_t *vmm_pte(uint32_t pd, uint32_t va, int create)
{
    uint32_t *pde = &PD(pd)[va >> 22];
    if (!(*pde & PTE_P)) {
        if (!create)
            return 0;
        uint32_t pt = kframe_alloc();
        if (!pt)
            return 0;
        memset((void *)pt, 0, PAGE_SIZE);
        *pde = pt | PTE_P | PTE_W | PTE_U;
    }
    return &((uint32_t *)(*pde & PAGE_MASK))[(va >> 12) & 1023];
}

/*
 * Make the user page at va present and return a kernel pointer to it.
 * The pointer is only valid until the next frame allocation (which may
 * compress this very page).
 */
uint8_t *vmm_user_page(struct proc *owner, uint32_t pd, uint32_t va, int alloc)
{
    va &= PAGE_MASK;
    uint32_t *pte = vmm_pte(pd, va, alloc);
    if (!pte)
        return 0;
    if (*pte & PTE_P) {
        *pte |= PTE_A;
        return (uint8_t *)(*pte & PAGE_MASK);
    }
    if (*pte & PTE_Z) {
        if (zram_page_in(owner, pd, va, pte) < 0)
            return 0;
        return (uint8_t *)(*pte & PAGE_MASK);
    }
    if (!alloc)
        return 0;
    uint32_t f = uframe_alloc(owner, pd, va);
    if (!f)
        return 0;
    memset((void *)f, 0, PAGE_SIZE);
    *pte = f | PTE_P | PTE_W | PTE_U | PTE_A;
    return (uint8_t *)f;
}

void vmm_unmap_range(struct proc *owner, uint32_t pd, uint32_t start, uint32_t end)
{
    int cur = read_cr3() == pd;
    for (uint32_t va = start & PAGE_MASK; va < end; ) {
        uint32_t *pde = &PD(pd)[va >> 22];
        if (!(*pde & PTE_P)) {
            va = (va & ~0x3FFFFFu) + 0x400000u;
            continue;
        }
        uint32_t *pte = vmm_pte(pd, va, 0);
        if (*pte & PTE_PHYS) {
            /* device memory: just drop the mapping */
        } else if (*pte & PTE_P) {
            uframe_release(*pte & PAGE_MASK);
        } else if (*pte & PTE_Z) {
            zram_slot_free(*pte >> 12);
            if (owner)
                owner->compressed--;
        }
        *pte = 0;
        if (cur)
            invlpg(va);
        va += PAGE_SIZE;
    }
}

void vmm_free_pd(struct proc *owner, uint32_t pd)
{
    if (!pd)
        return;
    vmm_unmap_range(owner, pd, USER_BASE, USER_END);
    for (int i = KERNEL_PDES; i < 1024; i++) {
        if (PD(pd)[i] & PTE_P)
            pmm_free(PD(pd)[i] & PAGE_MASK);
    }
    pmm_free(pd);
}

/* fork: duplicate the user half of spd into dpd.  Compressed pages are
 * shared copy-free by bumping the zram slot reference count. */
int vmm_copy_space(struct proc *dst, uint32_t dpd, uint32_t spd)
{
    for (uint32_t va = USER_BASE; va < USER_END; ) {
        if (!(PD(spd)[va >> 22] & PTE_P)) {
            va = (va & ~0x3FFFFFu) + 0x400000u;
            continue;
        }
        uint32_t *spte = vmm_pte(spd, va, 0);
        if (*spte & (PTE_P | PTE_Z)) {
            uint32_t *dpte = vmm_pte(dpd, va, 1);
            if (!dpte)
                return -ENOMEM;
            if (*spte & PTE_PHYS) {
                *dpte = *spte;                  /* device memory is shared */
            } else if (*spte & PTE_P) {
                uint32_t f = uframe_alloc(dst, dpd, va);
                if (!f)
                    return -ENOMEM;
                spte = vmm_pte(spd, va, 0);     /* may have been compressed */
                if (*spte & PTE_P) {
                    memcpy((void *)f, (void *)(*spte & PAGE_MASK), PAGE_SIZE);
                    *dpte = f | (*spte & (PTE_U | PTE_W)) | PTE_P | PTE_A;
                } else {
                    uframe_release(f);
                    goto share;
                }
            } else {
share:
                zram_slot_dup(*spte >> 12);
                *dpte = *spte;
                dst->compressed++;
            }
        }
        va += PAGE_SIZE;
    }
    return 0;
}

void page_fault(struct regs *r)
{
    uint32_t addr = read_cr2();
    struct proc *p = current;

    if (p && p->pd && addr >= USER_BASE && addr < USER_END) {
        uint32_t *pte = vmm_pte(p->pd, addr, 0);
        if (pte && (*pte & PTE_Z)) {
            if (zram_page_in(p, p->pd, addr & PAGE_MASK, pte) == 0)
                return;
            kprintf("%s[%d]: out of memory paging in %x\n", p->name, p->pid, addr);
            do_exit(9);
        }
        int demand = (addr >= p->heap_start && addr < p->brk) ||
                     (addr >= USER_STACK_TOP - USER_STACK_MAX) || vma_anon(p, addr);
        if (demand && !(pte && (*pte & PTE_P))) {
            if (vmm_user_page(p, p->pd, addr, 1))
                return;
            kprintf("%s[%d]: out of memory at %x\n", p->name, p->pid, addr);
            do_exit(9);
        }
    }
    if ((r->cs & 3) == 3 || (p && p->pd && addr >= USER_BASE)) {
        kprintf("\033[31m%s[%d]: segmentation fault at %x (eip=%x err=%x)\033[0m\n",
                p ? p->name : "?", p ? p->pid : -1, addr, r->eip, r->err);
        do_exit(11);
    }
    panic("kernel page fault at %x eip=%x err=%x", addr, r->eip, r->err);
}
