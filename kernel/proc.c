/*
 * Processes: creation (fork / exec / spawn), ELF loading with #! scripts,
 * exit/wait, a round-robin scheduler and the idle thread.
 *
 * The kernel is not preemptible: interrupts are off while in kernel mode
 * (except in the idle thread), and the timer only forces a reschedule when
 * it interrupts user mode.
 */
#include "kernel.h"

#define KSTACK_SIZE 16384
#define MAX_ARGS    64
#define MAX_ARGLEN  4096

struct proc procs[NPROC];
struct proc *current;
int need_resched;
static struct proc *idle;
static int next_pid = 1;
static uint8_t fpu_init_state[512] __attribute__((aligned(16)));

extern void switch_context(uint32_t *save, uint32_t new_esp);
extern void trap_return(void);

void proc_init(void)
{
    uint32_t cr0, cr4;
    __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
    cr0 &= ~(1u << 2);                  /* EM off */
    cr0 |= (1u << 1) | (1u << 5);       /* MP, NE */
    __asm__ volatile("mov %0,%%cr0" :: "r"(cr0));
    __asm__ volatile("mov %%cr4,%0" : "=r"(cr4));
    cr4 |= (1u << 9) | (1u << 10);      /* OSFXSR, OSXMMEXCPT */
    __asm__ volatile("mov %0,%%cr4" :: "r"(cr4));
    __asm__ volatile("fninit; fxsave (%0)" :: "r"(fpu_init_state) : "memory");

    idle = &procs[0];
    idle->state = P_READY;
    strlcpy(idle->name, "idle", sizeof idle->name);
    strlcpy(idle->cwd, "/", sizeof idle->cwd);
    current = idle;
}

struct proc *proc_by_pid(int pid)
{
    for (int i = 0; i < NPROC; i++)
        if (procs[i].state != P_UNUSED && procs[i].pid == pid)
            return &procs[i];
    return 0;
}

static struct proc *proc_alloc(void)
{
    for (int i = 1; i < NPROC; i++) {
        struct proc *p = &procs[i];
        if (p->state != P_UNUSED)
            continue;
        memset(p, 0, sizeof *p);
        p->kstack = (uint32_t)kmalloc(KSTACK_SIZE);
        p->fpu_raw = kmalloc(512 + 16);
        if (!p->kstack || !p->fpu_raw) {
            kfree((void *)p->kstack);
            kfree(p->fpu_raw);
            return 0;
        }
        p->fpu = (uint8_t *)(((uint32_t)p->fpu_raw + 15) & ~15u);
        memcpy(p->fpu, fpu_init_state, 512);
        p->pid = next_pid++;
        p->state = P_BLOCKED;           /* not runnable until set up */
        return p;
    }
    return 0;
}

static void proc_free(struct proc *p)
{
    kfree((void *)p->kstack);
    kfree(p->fpu_raw);
    p->state = P_UNUSED;
}

/* build a kernel stack that "returns" to user mode with frame tf */
static void setup_kstack(struct proc *p, const struct regs *tf_src)
{
    uint32_t top = p->kstack + KSTACK_SIZE;
    struct regs *tf = (struct regs *)(top - sizeof(struct regs));
    *tf = *tf_src;
    uint32_t *sp = (uint32_t *)tf;
    *--sp = (uint32_t)trap_return;
    for (int i = 0; i < 4; i++)
        *--sp = 0;                      /* ebp ebx esi edi */
    p->kesp = (uint32_t)sp;
    p->tf = tf;
}

static void user_regs(struct regs *r, uint32_t entry, uint32_t sp)
{
    memset(r, 0, sizeof *r);
    r->gs = r->fs = r->es = r->ds = r->ss = UDATA_SEL;
    r->cs = UCODE_SEL;
    r->eflags = 0x202;
    r->eip = entry;
    r->useresp = sp;
}

/* ---- scheduler ---- */
static void switch_to(struct proc *next)
{
    struct proc *prev = current;
    if (prev->fpu)
        __asm__ volatile("fxsave (%0)" :: "r"(prev->fpu) : "memory");
    current = next;
    if (next->kstack)
        tss_set_kstack(next->kstack + KSTACK_SIZE);
    write_cr3(next->pd ? next->pd : kernel_pd);
    if (next->fpu)
        __asm__ volatile("fxrstor (%0)" :: "r"(next->fpu) : "memory");
    switch_context(&prev->kesp, next->kesp);
}

void schedule(void)
{
    need_resched = 0;
    int start = (int)(current - procs);
    struct proc *next = 0;
    for (int i = 1; i <= NPROC; i++) {
        struct proc *p = &procs[(start + i) % NPROC];
        if (p != idle && p->state == P_READY) {
            next = p;
            break;
        }
    }
    if (!next)
        next = idle;
    if (next != current)
        switch_to(next);
}

void sleep_on(void *chan)
{
    current->wait_chan = chan;
    current->state = P_BLOCKED;
    schedule();
    current->wait_chan = 0;
}

void wakeup(void *chan)
{
    for (int i = 0; i < NPROC; i++) {
        if (procs[i].state == P_BLOCKED && procs[i].wait_chan == chan) {
            procs[i].state = P_READY;
            need_resched = 1;
        }
    }
}

void idle_loop(void)
{
    for (;;) {
        schedule();
        zram_idle_scan();
        sti();
        hlt();
        cli();
    }
}

/* ---- program loading ---- */
struct elf_hdr {
    uint8_t  ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct elf_ph {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
};

static int write_user(struct proc *p, uint32_t pd, uint32_t va, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    while (n) {
        uint8_t *pg = vmm_user_page(p, pd, va, 1);
        if (!pg)
            return -ENOMEM;
        uint32_t o = va & (PAGE_SIZE - 1);
        uint32_t c = PAGE_SIZE - o < n ? PAGE_SIZE - o : n;
        memcpy(pg + o, s, c);
        s += c;
        va += c;
        n -= c;
    }
    return 0;
}

static int load_elf(struct proc *p, uint32_t pd, const uint8_t *img, uint32_t len,
                    uint32_t *entry, uint32_t *brk)
{
    const struct elf_hdr *h = (const struct elf_hdr *)img;
    if (len < sizeof *h || memcmp(h->ident, "\177ELF", 4) != 0 ||
        h->ident[4] != 1 || h->machine != 3 || h->type != 2)
        return -ENOEXEC;
    if (h->phoff + (uint32_t)h->phnum * sizeof(struct elf_ph) > len)
        return -ENOEXEC;
    uint32_t end = USER_BASE;
    for (int i = 0; i < h->phnum; i++) {
        const struct elf_ph *ph = (const struct elf_ph *)(img + h->phoff) + i;
        if (ph->type != 1 || ph->memsz == 0)
            continue;
        if (ph->vaddr < USER_BASE || ph->vaddr + ph->memsz > USER_STACK_TOP - USER_STACK_MAX ||
            ph->filesz > ph->memsz || ph->offset + ph->filesz > len)
            return -ENOEXEC;
        /* touch every page (zeroed), then copy the file-backed part */
        for (uint32_t va = ph->vaddr & PAGE_MASK; va < ph->vaddr + ph->memsz; va += PAGE_SIZE)
            if (!vmm_user_page(p, pd, va, 1))
                return -ENOMEM;
        int r = write_user(p, pd, ph->vaddr, img + ph->offset, ph->filesz);
        if (r < 0)
            return r;
        if (ph->vaddr + ph->memsz > end)
            end = ph->vaddr + ph->memsz;
    }
    *entry = h->entry;
    *brk = ALIGN_UP(end, PAGE_SIZE);
    return 0;
}

/* lay out argc/argv/envp at the top of the new stack */
static int setup_stack(struct proc *p, uint32_t pd, char **argv, int argc, uint32_t *sp_out)
{
    uint32_t slen = 0;
    for (int i = 0; i < argc; i++)
        slen += strlen(argv[i]) + 1;
    uint32_t hdr = 12 + 4 * (argc + 1) + 4;
    uint32_t size = ALIGN_UP(hdr + slen, 16u);
    uint32_t sp = USER_STACK_TOP - size;
    uint8_t *buf = kzalloc(size);
    if (!buf)
        return -ENOMEM;
    uint32_t *w = (uint32_t *)buf;
    uint32_t argv_va = sp + 12, envp_va = sp + 12 + 4 * (argc + 1);
    w[0] = (uint32_t)argc;
    w[1] = argv_va;
    w[2] = envp_va;
    uint32_t so = hdr;
    for (int i = 0; i < argc; i++) {
        w[3 + i] = sp + so;
        uint32_t l = strlen(argv[i]) + 1;
        memcpy(buf + so, argv[i], l);
        so += l;
    }
    w[3 + argc] = 0;                    /* argv terminator */
    w[4 + argc] = 0;                    /* empty envp */
    int r = write_user(p, pd, sp, buf, size);
    kfree(buf);
    *sp_out = sp;
    return r;
}

struct image {
    uint32_t pd, entry, sp, brk;
};

static int load_program(struct proc *p, const char *path, char **argv, int argc,
                        struct image *im, int depth)
{
    struct inode *ino = ramfs_lookup(path, p->cwd);
    if (!ino)
        return -ENOENT;
    if (ramfs_isdir(ino))
        return -EISDIR;
    uint32_t len;
    uint8_t *img = ramfs_read_all(ino, &len);
    if (!img)
        return -ENOMEM;

    if (len > 2 && img[0] == '#' && img[1] == '!') {
        /* script: #!interpreter [arg] */
        char line[128];
        uint32_t i = 2, o = 0;
        while (i < len && img[i] == ' ')
            i++;
        while (i < len && img[i] != '\n' && o < sizeof line - 1)
            line[o++] = (char)img[i++];
        line[o] = 0;
        kfree(img);
        while (o > 0 && (line[o - 1] == ' ' || line[o - 1] == '\r'))
            line[--o] = 0;
        if (depth > 2 || !o)
            return -ENOEXEC;
        char *interp = line, *arg = strchr(line, ' ');
        if (arg) {
            *arg++ = 0;
            while (*arg == ' ')
                arg++;
            if (!*arg)
                arg = 0;
        }
        char *nargv[MAX_ARGS + 3];
        int n = 0;
        nargv[n++] = interp;
        if (arg)
            nargv[n++] = arg;
        nargv[n++] = (char *)path;
        for (int k = 1; k < argc && n < MAX_ARGS + 2; k++)
            nargv[n++] = argv[k];
        nargv[n] = 0;
        return load_program(p, interp, nargv, n, im, depth + 1);
    }

    uint32_t pd = vmm_new_pd();
    int r = pd ? 0 : -ENOMEM;
    if (r == 0)
        r = load_elf(p, pd, img, len, &im->entry, &im->brk);
    kfree(img);
    if (r == 0)
        r = setup_stack(p, pd, argv, argc, &im->sp);
    if (r < 0) {
        vmm_free_pd(p, pd);
        return r;
    }
    im->pd = pd;
    return 0;
}

static void set_name(struct proc *p, const char *path, char **argv, int argc)
{
    /* scripts are named after the script, not the interpreter */
    const char *n = argc > 0 ? argv[0] : path;
    const char *s = n;
    for (const char *c = n; *c; c++)
        if (*c == '/')
            s = c + 1;
    strlcpy(p->name, s, sizeof p->name);
}

static void free_argv(char **argv, int argc)
{
    for (int i = 0; i < argc; i++)
        kfree(argv[i]);
}

static int copy_argv(char *const *uargv, char **argv, int *argc)
{
    int n = 0;
    char *tmp = kmalloc(MAX_ARGLEN);
    if (!tmp)
        return -ENOMEM;
    if (uargv) {
        for (;;) {
            char *up;
            if (copy_from_user(&up, uargv + n, sizeof up) < 0) {
                free_argv(argv, n);
                kfree(tmp);
                return -EFAULT;
            }
            if (!up)
                break;
            if (n >= MAX_ARGS || copy_str_from_user(tmp, up, MAX_ARGLEN) < 0 ||
                !(argv[n] = kstrdup(tmp))) {
                free_argv(argv, n);
                kfree(tmp);
                return -E2BIG;
            }
            n++;
        }
    }
    argv[n] = 0;
    *argc = n;
    kfree(tmp);
    return 0;
}

int proc_execve(struct regs *r, const char *path, char *const *uargv)
{
    char *argv[MAX_ARGS + 1];
    int argc;
    int e = copy_argv(uargv, argv, &argc);
    if (e < 0)
        return e;
    struct image im;
    e = load_program(current, path, argv, argc, &im, 0);
    if (e == 0) {
        uint32_t old = current->pd;
        current->pd = im.pd;
        write_cr3(im.pd);
        vmm_free_pd(current, old);
        current->heap_start = current->brk = im.brk;
        vma_reset(current);
        current->iopl = 0;
        set_name(current, path, argv, argc);
        user_regs(r, im.entry, im.sp);
        __asm__ volatile("fxrstor (%0)" :: "r"(fpu_init_state));
    }
    free_argv(argv, argc);
    return e;
}

int proc_spawn(const struct kiwi_spawn *sp)
{
    char path[256];
    char *argv[MAX_ARGS + 1];
    int argc, e;
    if ((e = copy_str_from_user(path, sp->path, sizeof path)) < 0)
        return e;
    if ((e = copy_argv(sp->argv, argv, &argc)) < 0)
        return e;
    struct proc *p = proc_alloc();
    if (!p) {
        free_argv(argv, argc);
        return -EAGAIN;
    }
    p->ppid = current->pid;
    p->pgid = sp->pgid == 0 ? current->pgid : sp->pgid < 0 ? p->pid : sp->pgid;
    strlcpy(p->cwd, current->cwd, sizeof p->cwd);
    int map[3] = { sp->fd_in, sp->fd_out, sp->fd_err };
    for (int i = 0; i < 3; i++) {
        int src = map[i] >= 0 ? map[i] : i;
        if (src < NFD && current->fds[src])
            p->fds[i] = file_dup(current->fds[src]);
    }
    struct image im;
    e = load_program(p, path, argv, argc, &im, 0);
    if (e < 0) {
        for (int i = 0; i < 3; i++)
            if (p->fds[i])
                file_close(p->fds[i]);
        free_argv(argv, argc);
        proc_free(p);
        return e;
    }
    p->pd = im.pd;
    p->heap_start = p->brk = im.brk;
    vma_reset(p);
    set_name(p, path, argv, argc);
    free_argv(argv, argc);
    struct regs r;
    user_regs(&r, im.entry, im.sp);
    setup_kstack(p, &r);
    p->state = P_READY;
    return p->pid;
}

int proc_fork(struct regs *r)
{
    struct proc *p = proc_alloc();
    if (!p)
        return -EAGAIN;
    uint32_t pd = vmm_new_pd();
    if (!pd || vmm_copy_space(p, pd, current->pd) < 0) {
        vmm_free_pd(p, pd);
        proc_free(p);
        return -ENOMEM;
    }
    p->pd = pd;
    p->ppid = current->pid;
    p->pgid = current->pgid;
    p->heap_start = current->heap_start;
    p->brk = current->brk;
    p->iopl = current->iopl;
    p->nvma = current->nvma;
    p->mmap_next = current->mmap_next;
    memcpy(p->vma, current->vma, sizeof p->vma);
    strlcpy(p->cwd, current->cwd, sizeof p->cwd);
    strlcpy(p->name, current->name, sizeof p->name);
    for (int i = 0; i < NFD; i++)
        if (current->fds[i])
            p->fds[i] = file_dup(current->fds[i]);
    __asm__ volatile("fxsave (%0)" :: "r"(p->fpu) : "memory");
    struct regs cr = *r;
    cr.eax = 0;
    setup_kstack(p, &cr);
    p->state = P_READY;
    return p->pid;
}

void proc_start_init(const char *path)
{
    struct proc *p = proc_alloc();
    p->pgid = p->pid;
    strlcpy(p->cwd, "/", sizeof p->cwd);
    for (int i = 0; i < 3; i++)
        p->fds[i] = file_console();
    char *argv[2] = { (char *)path, 0 };
    struct image im;
    int e = load_program(p, path, argv, 1, &im, 0);
    if (e < 0)
        panic("cannot start %s (error %d)", path, e);
    p->pd = im.pd;
    p->heap_start = p->brk = im.brk;
    vma_reset(p);
    set_name(p, path, argv, 1);
    struct regs r;
    user_regs(&r, im.entry, im.sp);
    setup_kstack(p, &r);
    p->state = P_READY;
}

void do_exit(int status)
{
    struct proc *p = current;
    if (p->pid == 1)
        panic("init exited with status %x", status);
    for (int i = 0; i < NFD; i++) {
        if (p->fds[i]) {
            file_close(p->fds[i]);
            p->fds[i] = 0;
        }
    }
    uint32_t pd = p->pd;
    p->pd = 0;
    write_cr3(kernel_pd);
    vmm_free_pd(p, pd);

    struct proc *init = proc_by_pid(1);
    for (int i = 0; i < NPROC; i++) {
        if (procs[i].state != P_UNUSED && procs[i].ppid == p->pid) {
            procs[i].ppid = 1;
            if (procs[i].state == P_ZOMBIE)
                wakeup(init);
        }
    }
    p->exit_status = status;
    p->state = P_ZOMBIE;
    struct proc *parent = proc_by_pid(p->ppid);
    if (parent)
        wakeup(parent);
    schedule();
    panic("zombie scheduled");
}

int proc_waitpid(int pid, int *ustatus, int options)
{
    for (;;) {
        int have = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *c = &procs[i];
            if (c->state == P_UNUSED || c == idle || c->ppid != current->pid)
                continue;
            if (pid > 0 && c->pid != pid)
                continue;
            have = 1;
            if (c->state == P_ZOMBIE) {
                int cpid = c->pid, st = c->exit_status;
                proc_free(c);
                if (ustatus && copy_to_user(ustatus, &st, sizeof st) < 0)
                    return -EFAULT;
                return cpid;
            }
        }
        if (!have)
            return -ECHILD;
        if (options & WNOHANG)
            return 0;
        sleep_on(current);
        if (current->killed)
            return -EINTR;
    }
}

int proc_kill(int pid, int sig)
{
    struct proc *p = proc_by_pid(pid);
    if (!p || p == idle || p->state == P_ZOMBIE)
        return -ESRCH;
    if (p->pid == 1)
        return -EPERM;
    if (sig == 0)
        return 0;
    p->killed = sig;
    if (p->state == P_BLOCKED) {
        p->wake_tick = 0;
        p->state = P_READY;
        need_resched = 1;
    }
    return 0;
}

void proc_kill_pgrp(int pgid)
{
    for (int i = 1; i < NPROC; i++)
        if (procs[i].state != P_UNUSED && procs[i].pgid == pgid)
            proc_kill(procs[i].pid, SIGINT);
}

int proc_list(struct kiwi_procinfo *buf, int max)
{
    int n = 0;
    for (int i = 0; i < NPROC && n < max; i++) {
        struct proc *p = &procs[i];
        if (p->state == P_UNUSED)
            continue;
        struct kiwi_procinfo pi;
        memset(&pi, 0, sizeof pi);
        pi.pid = p->pid;
        pi.ppid = p->ppid;
        pi.pgid = p->pgid;
        pi.state = p->state == P_ZOMBIE ? PSTATE_ZOMBIE :
                   p->state == P_BLOCKED ? PSTATE_BLOCKED : PSTATE_RUNNING;
        pi.resident = p->resident;
        pi.compressed = p->compressed;
        pi.ticks = p->ticks;
        strlcpy(pi.name, p->name, sizeof pi.name);
        if (copy_to_user(buf + n, &pi, sizeof pi) < 0)
            return -EFAULT;
        n++;
    }
    return n;
}

int proc_sbrk(int incr)
{
    struct proc *p = current;
    uint32_t old = p->brk, nb = old + (uint32_t)incr;
    if ((incr > 0 && nb < old) || (incr < 0 && nb > old) ||
        nb < p->heap_start || nb > MMAP_BASE)
        return -ENOMEM;
    if (nb < old)
        vmm_unmap_range(p, p->pd, ALIGN_UP(nb, PAGE_SIZE), ALIGN_UP(old, PAGE_SIZE));
    p->brk = nb;
    return (int)old;
}
