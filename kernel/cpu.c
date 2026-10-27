/* GDT/TSS, IDT, 8259 PIC, PIT, RTC and the central trap dispatcher. */
#include "kernel.h"

struct gdt_entry {
    uint16_t limit_lo, base_lo;
    uint8_t  base_mid, access, gran, base_hi;
} __attribute__((packed));

struct dtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss {
    uint32_t prev, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap;
} __attribute__((packed));

struct idt_entry {
    uint16_t off_lo, sel;
    uint8_t  zero, flags;
    uint16_t off_hi;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct tss tss;
static struct idt_entry idt[256];
volatile uint32_t ticks;
uint32_t irq_counts[KINFO_NVEC];
uint32_t syscall_count;

extern void gdt_flush(uint32_t);

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[i].limit_lo = (uint16_t)limit;
    gdt[i].base_lo = (uint16_t)base;
    gdt[i].base_mid = (uint8_t)(base >> 16);
    gdt[i].access = access;
    gdt[i].gran = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    gdt[i].base_hi = (uint8_t)(base >> 24);
}

void gdt_init(void)
{
    static struct dtr gdtr;
    gdt_set(0, 0, 0, 0, 0);
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF);    /* kernel code */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF);    /* kernel data */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xCF);    /* user code */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xCF);    /* user data */
    memset(&tss, 0, sizeof tss);
    tss.ss0 = KDATA_SEL;
    tss.iomap = sizeof tss;
    gdt_set(5, (uint32_t)&tss, sizeof tss - 1, 0x89, 0x00);
    gdtr.limit = sizeof gdt - 1;
    gdtr.base = (uint32_t)gdt;
    gdt_flush((uint32_t)&gdtr);
}

void tss_set_kstack(uint32_t esp0)
{
    tss.esp0 = esp0;
}

static void idt_set(int n, void (*fn)(void), uint8_t flags)
{
    uint32_t a = (uint32_t)fn;
    idt[n].off_lo = (uint16_t)a;
    idt[n].sel = KCODE_SEL;
    idt[n].zero = 0;
    idt[n].flags = flags;
    idt[n].off_hi = (uint16_t)(a >> 16);
}

#define X(n) extern void isr##n(void);
X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15)
X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)
X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47)
X(128)
#undef X

static void (*const stubs[48])(void) = {
    isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7, isr8, isr9, isr10, isr11,
    isr12, isr13, isr14, isr15, isr16, isr17, isr18, isr19, isr20, isr21, isr22,
    isr23, isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31, isr32, isr33,
    isr34, isr35, isr36, isr37, isr38, isr39, isr40, isr41, isr42, isr43, isr44,
    isr45, isr46, isr47,
};

static void pic_remap(void)
{
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0xE8);           /* enable IRQ0 timer, 1 kbd, 2 cascade, 4 com1 */
    outb(0xA1, 0xEF);           /* slave: IRQ12 PS/2 mouse */
}

void pic_eoi(int irq)
{
    if (irq >= 8)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void idt_init(void)
{
    static struct dtr idtr;
    for (int i = 0; i < 48; i++)
        idt_set(i, stubs[i], 0x8E);
    idt_set(0x80, isr128, 0xEE);        /* syscall gate, DPL 3 */
    idtr.limit = sizeof idt - 1;
    idtr.base = (uint32_t)idt;
    __asm__ volatile("lidt %0" :: "m"(idtr));
    pic_remap();
}

void pit_init(int hz)
{
    uint32_t div = 1193182 / (uint32_t)hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)div);
    outb(0x40, (uint8_t)(div >> 8));
}

/* ---- RTC: boot wall-clock time ---- */
uint32_t boot_epoch;

static uint8_t cmos(uint8_t r)
{
    outb(0x70, r);
    return inb(0x71);
}

static int bcd(int v) { return (v & 15) + (v >> 4) * 10; }

void rtc_init(void)
{
    while (cmos(0x0A) & 0x80)
        ;
    int s = cmos(0), mi = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9);
    if (!(cmos(0x0B) & 4)) {
        s = bcd(s); mi = bcd(mi); h = bcd(h); d = bcd(d); mo = bcd(mo); y = bcd(y);
    }
    y += 2000;
    /* days from civil (Howard Hinnant) */
    y -= mo <= 2;
    int era = y / 400;
    int yoe = y - era * 400;
    int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int days = era * 146097 + doe - 719468;
    boot_epoch = (uint32_t)days * 86400u + (uint32_t)(h * 3600 + mi * 60 + s);
}

void poweroff(void)
{
    kprintf("Kiwi: power off.\n");
    outw(0x604, 0x2000);        /* QEMU PIIX4/ICH9 ACPI */
    outw(0xB004, 0x2000);       /* older QEMU / Bochs */
    outb(0xF4, 0x00);           /* isa-debug-exit if present */
    cli();
    for (;;)
        hlt();
}

/* ---- trap dispatch ---- */
static const char *const exc_names[32] = {
    "divide error", "debug", "NMI", "breakpoint", "overflow", "bound",
    "invalid opcode", "no FPU", "double fault", "coproc overrun", "bad TSS",
    "segment not present", "stack fault", "general protection", "page fault",
    "reserved", "x87 FPU error", "alignment check", "machine check", "SIMD FP",
};

void isr_dispatch(struct regs *r)
{
    int from_user = (r->cs & 3) == 3;
    if (from_user && current)
        current->tf = r;
    irq_counts[r->int_no == 0x80 ? KINFO_NVEC - 1 : r->int_no < KINFO_NVEC - 1 ? r->int_no : 0]++;

    if (r->int_no == 0x80) {
        syscall_dispatch(r);
    } else if (r->int_no == 14) {
        page_fault(r);
    } else if (r->int_no < 32) {
        const char *nm = exc_names[r->int_no] ? exc_names[r->int_no] : "exception";
        if (from_user) {
            kprintf("\033[31m%s[%d]: %s at eip=%x\033[0m\n",
                    current->name, current->pid, nm, r->eip);
            do_exit(11);       /* SIGSEGV */
        }
        panic("%s (err=%x) at eip=%x", nm, r->err, r->eip);
    } else if (r->int_no < 48) {
        int irq = (int)r->int_no - 32;
        if (irq == 0) {
            ticks++;
            for (int i = 0; i < NPROC; i++) {
                struct proc *p = &procs[i];
                if (p->state == P_BLOCKED && p->wake_tick && ticks >= p->wake_tick) {
                    p->wake_tick = 0;
                    p->state = P_READY;
                }
            }
            if (from_user) {
                current->ticks++;
                if ((ticks & 3) == 0)
                    need_resched = 1;
            }
        } else if (irq == 1) {
            keyboard_irq();
        } else if (irq == 4) {
            serial_irq();
        } else if (irq == 12) {
            mouse_irq();
        }
        pic_eoi(irq);
    }

    if (from_user) {
        if (current->killed)
            do_exit(current->killed);
        if (need_resched)
            schedule();
    }
}

/* ---- introspection ---- */
void cpu_sysinfo(struct kiwi_sysinfo *si)
{
    struct dtr g, i;
    __asm__ volatile("sgdt %0" : "=m"(g));
    __asm__ volatile("sidt %0" : "=m"(i));
    __asm__ volatile("mov %%cr0,%0" : "=r"(si->cr0));
    __asm__ volatile("mov %%cr2,%0" : "=r"(si->cr2));
    __asm__ volatile("mov %%cr3,%0" : "=r"(si->cr3));
    __asm__ volatile("mov %%cr4,%0" : "=r"(si->cr4));
    __asm__ volatile("pushfl; popl %0" : "=r"(si->eflags));
    si->gdt_base = g.base;
    si->gdt_limit = g.limit;
    si->idt_base = i.base;
    si->idt_limit = i.limit;
    si->tss_esp0 = tss.esp0;
}

const void *cpu_table(int which, uint32_t *len)
{
    if (which == 0) {
        *len = sizeof gdt;
        return gdt;
    }
    *len = sizeof idt;
    return idt;
}
