/* Kiwi kernel entry point. */
#include "kernel.h"
#include "multiboot.h"

extern char kernel_start[], kernel_end[];
char kernel_cmdline[256];

static const char *cmdline_get(const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    const char *p = kernel_cmdline;
    while (*p) {
        while (*p == ' ')
            p++;
        if (strncmp(p, key, kl) == 0 && p[kl] == '=') {
            p += kl + 1;
            size_t o = 0;
            while (*p && *p != ' ' && o < n - 1)
                out[o++] = *p++;
            out[o] = 0;
            return out;
        }
        while (*p && *p != ' ')
            p++;
    }
    return 0;
}

static uint32_t atou(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

void kmain(uint32_t magic, struct multiboot_info *mbi)
{
    serial_init();
    console_init();
    kprintf("\033[1;32mKiwi\033[0m " KIWI_RELEASE " - a tiny Unix-like kernel with compressed memory\n");
    if (magic != MB_BOOT_MAGIC)
        panic("not booted by a multiboot loader (magic %x)", magic);

    if (mbi->flags & MB_INFO_CMDLINE)
        strlcpy(kernel_cmdline, (const char *)mbi->cmdline, sizeof kernel_cmdline);

    uint32_t reserve_end = (uint32_t)kernel_end;
    uint32_t rd_start = 0, rd_end = 0;
    if ((mbi->flags & MB_INFO_MODS) && mbi->mods_count > 0) {
        struct mb_module *m = (struct mb_module *)mbi->mods_addr;
        rd_start = m->start;
        rd_end = m->end;
        for (uint32_t i = 0; i < mbi->mods_count; i++)
            if (m[i].end > reserve_end)
                reserve_end = m[i].end;
    }
    reserve_end = ALIGN_UP(reserve_end, PAGE_SIZE);

    gdt_init();
    idt_init();
    pmm_init(mbi, reserve_end);

    uint32_t ram = pmm_total_frames * PAGE_SIZE;
    uint32_t heap = ram / 4;
    if (heap < (8u << 20)) heap = 8u << 20;
    if (heap > (256u << 20)) heap = 256u << 20;
    pmm_reserve(reserve_end, reserve_end + heap);
    heap_init(reserve_end, heap);
    vmm_init();
    kprintf("memory: %d MB RAM, kernel heap %d MB at %x\n",
            ram >> 20, heap >> 20, reserve_end);

    pit_init(HZ);
    pci_init();
    fb_init();
    input_init();
    rtc_init();
    proc_init();
    ramfs_init();
    dev_init();
    if (rd_start)
        ramfs_load_initrd((const uint8_t *)rd_start, rd_end - rd_start);
    else
        kprintf("warning: no initrd module\n");
    if (rd_start)
        pmm_release(rd_start & PAGE_MASK, ALIGN_UP(rd_end, PAGE_SIZE));

    zram_init();
    char val[64];
    if (cmdline_get("zbudget", val, sizeof val))
        zram_set_budget(atou(val) * 256);          /* in MB */
    if (cmdline_get("zproactive", val, sizeof val))
        zram_proactive = atou(val);
    kprintf("zram: user page budget %d pages (%d MB), proactive idle compression %s\n",
            zram_budget, zram_budget / 256, zram_proactive ? "on" : "off");

    const char *init = cmdline_get("init", val, sizeof val) ? val : "/bin/init";
    proc_start_init(init);
    idle_loop();
}
