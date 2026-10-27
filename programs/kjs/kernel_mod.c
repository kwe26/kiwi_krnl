/*
 * The 'kernel' module: Kiwi's low-level internals for JavaScript.
 *
 *   import * as kernel from 'kernel';
 *   kernel.outb(0x70, 0x00); kernel.inb(0x71);     // raw port I/O (iopl 3)
 *   kernel.cpuid(0); kernel.rdtsc();                // the CPU
 *   kernel.system(); kernel.irqs(); kernel.pci();   // kernel state
 *   kernel.gdt(); kernel.idt(); kernel.pagemap(pid) // raw tables (ArrayBuffers)
 *   kernel.peek(0xB8000, 160);                      // physical memory
 */
#include "quickjs.h"

#ifdef KJS_HOST
/* host build (syntax checks only): an empty module so imports resolve */
static int host_init(JSContext *ctx, JSModuleDef *m) { return 0; }
JSModuleDef *kjs_module_kernel(JSContext *ctx, const char *name)
{
    return JS_NewCModule(ctx, name, host_init);
}
#else
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/io.h>
#include <kiwi/kiwi.h>
#include <kiwi/cpu.h>

static int have_iopl;

static JSValue throw_errno(JSContext *ctx, const char *what)
{
    return JS_ThrowInternalError(ctx, "%s: %s", what, strerror(errno));
}

static int ensure_iopl(JSContext *ctx)
{
    if (!have_iopl) {
        if (iopl(3) < 0) {
            throw_errno(ctx, "iopl");
            return -1;
        }
        have_iopl = 1;
    }
    return 0;
}

static uint32_t u32(JSContext *ctx, JSValueConst v)
{
    uint32_t x = 0;
    JS_ToUint32(ctx, &x, v);
    return x;
}

static JSValue k_in(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv, int size)
{
    if (ensure_iopl(ctx) < 0)
        return JS_EXCEPTION;
    uint16_t port = (uint16_t)u32(ctx, argv[0]);
    uint32_t v = size == 1 ? inb(port) : size == 2 ? inw(port) : inl(port);
    return JS_NewUint32(ctx, v);
}

static JSValue k_out(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv, int size)
{
    if (ensure_iopl(ctx) < 0)
        return JS_EXCEPTION;
    uint16_t port = (uint16_t)u32(ctx, argv[0]);
    uint32_t v = u32(ctx, argv[1]);
    if (size == 1) outb((uint8_t)v, port);
    else if (size == 2) outw((uint16_t)v, port);
    else outl(v, port);
    return JS_UNDEFINED;
}

static JSValue k_inb(JSContext *c, JSValueConst t, int n, JSValueConst *a) { return k_in(c, t, n, a, 1); }
static JSValue k_inw(JSContext *c, JSValueConst t, int n, JSValueConst *a) { return k_in(c, t, n, a, 2); }
static JSValue k_inl(JSContext *c, JSValueConst t, int n, JSValueConst *a) { return k_in(c, t, n, a, 4); }
static JSValue k_outb(JSContext *c, JSValueConst t, int n, JSValueConst *a) { return k_out(c, t, n, a, 1); }
static JSValue k_outw(JSContext *c, JSValueConst t, int n, JSValueConst *a) { return k_out(c, t, n, a, 2); }
static JSValue k_outl(JSContext *c, JSValueConst t, int n, JSValueConst *a) { return k_out(c, t, n, a, 4); }

static JSValue k_iopl(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int level = (int)u32(ctx, argv[0]);
    if (iopl(level) < 0)
        return throw_errno(ctx, "iopl");
    have_iopl = level == 3;
    return JS_UNDEFINED;
}

static JSValue k_cpuid(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    unsigned r[4];
    __cpuid_count(u32(ctx, argv[0]), argc > 1 ? u32(ctx, argv[1]) : 0, &r[0], &r[1], &r[2], &r[3]);
    JSValue a = JS_NewArray(ctx);
    for (int i = 0; i < 4; i++)
        JS_SetPropertyUint32(ctx, a, (uint32_t)i, JS_NewUint32(ctx, r[i]));
    return a;
}

static JSValue k_rdtsc(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewFloat64(ctx, (double)rdtsc());
}

static JSValue k_eflags(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    return JS_NewUint32(ctx, read_eflags());
}

static JSValue k_cpu(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    char vendor[13], brand[49];
    cpu_vendor(vendor);
    unsigned a, b, c, d;
    __cpuid_count(0x80000000u, 0, &a, &b, &c, &d);
    if (a >= 0x80000004u)
        cpu_brand(brand);
    else
        strcpy(brand, "unknown");
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "vendor", JS_NewString(ctx, vendor));
    char *p = brand;
    while (*p == ' ')
        p++;
    JS_SetPropertyStr(ctx, o, "brand", JS_NewString(ctx, p));
    return o;
}

/* kinfo into a malloc'd buffer */
static void *kinfo_buf(int what, int arg, size_t cap, int *len)
{
    void *b = malloc(cap);
    if (!b)
        return 0;
    *len = kinfo(what, arg, b, cap);
    if (*len < 0) {
        free(b);
        return 0;
    }
    return b;
}

static JSValue k_raw(JSContext *ctx, int what, int arg, size_t cap)
{
    int len;
    void *b = kinfo_buf(what, arg, cap, &len);
    if (!b)
        return throw_errno(ctx, "kinfo");
    JSValue v = JS_NewArrayBufferCopy(ctx, b, (size_t)len);
    free(b);
    return v;
}

static JSValue k_gdt(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { return k_raw(ctx, KINFO_GDT, 0, 256); }
static JSValue k_idt(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { return k_raw(ctx, KINFO_IDT, 0, 2048); }
static JSValue k_font(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { return k_raw(ctx, KINFO_FONT, 0, 4096); }

static JSValue k_pagemap(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int pid = argc > 0 ? (int)u32(ctx, argv[0]) : 0;
    int n;
    void *b = kinfo_buf(KINFO_PAGEMAP, pid, 64 * 1024 * sizeof(struct kiwi_pte), &n);
    if (!b)
        return throw_errno(ctx, "pagemap");
    JSValue v = JS_NewArrayBufferCopy(ctx, b, (size_t)n * sizeof(struct kiwi_pte));
    free(b);
    return v;                 /* pairs of u32: virtual address, PTE */
}

static JSValue k_dmesg(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    int n;
    char *b = kinfo_buf(KINFO_DMESG, 0, 16384, &n);
    if (!b)
        return throw_errno(ctx, "dmesg");
    JSValue v = JS_NewStringLen(ctx, b, (size_t)n);
    free(b);
    return v;
}

#define SETU(o, k, v) JS_SetPropertyStr(ctx, o, k, JS_NewUint32(ctx, (uint32_t)(v)))

static JSValue k_system(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct kiwi_sysinfo si;
    if (kinfo(KINFO_SYSTEM, 0, &si, sizeof si) < 0)
        return throw_errno(ctx, "kinfo");
    JSValue o = JS_NewObject(ctx);
    SETU(o, "cr0", si.cr0); SETU(o, "cr2", si.cr2); SETU(o, "cr3", si.cr3); SETU(o, "cr4", si.cr4);
    SETU(o, "eflags", si.eflags);
    SETU(o, "gdtBase", si.gdt_base); SETU(o, "gdtLimit", si.gdt_limit);
    SETU(o, "idtBase", si.idt_base); SETU(o, "idtLimit", si.idt_limit);
    SETU(o, "tssEsp0", si.tss_esp0);
    SETU(o, "kernelStart", si.kernel_start); SETU(o, "kernelEnd", si.kernel_end);
    SETU(o, "heapStart", si.heap_start); SETU(o, "heapSize", si.heap_size);
    SETU(o, "totalFrames", si.total_frames); SETU(o, "freeFrames", si.free_frames);
    SETU(o, "maxFrame", si.max_frame);
    SETU(o, "ticks", si.ticks); SETU(o, "hz", si.hz); SETU(o, "bootEpoch", si.boot_epoch);
    SETU(o, "procs", si.nprocs); SETU(o, "syscalls", si.syscalls);
    JS_SetPropertyStr(ctx, o, "cmdline", JS_NewString(ctx, si.cmdline));
    return o;
}

static JSValue k_irqs(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    uint32_t c[KINFO_NVEC];
    int n = kinfo(KINFO_IRQ, 0, c, sizeof c);
    if (n < 0)
        return throw_errno(ctx, "kinfo");
    JSValue a = JS_NewArray(ctx);
    for (int i = 0; i < n / 4; i++)
        JS_SetPropertyUint32(ctx, a, (uint32_t)i, JS_NewUint32(ctx, c[i]));
    return a;
}

static JSValue k_memmap(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct kiwi_memrange r[32];
    int n = kinfo(KINFO_MEMMAP, 0, r, sizeof r);
    if (n < 0)
        return throw_errno(ctx, "kinfo");
    JSValue a = JS_NewArray(ctx);
    for (int i = 0; i < n / (int)sizeof r[0]; i++) {
        JSValue o = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, o, "addr", JS_NewFloat64(ctx, (double)r[i].addr));
        JS_SetPropertyStr(ctx, o, "len", JS_NewFloat64(ctx, (double)r[i].len));
        SETU(o, "type", r[i].type);
        JS_SetPropertyUint32(ctx, a, (uint32_t)i, o);
    }
    return a;
}

static JSValue k_pci(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct kiwi_pcidev d[32];
    int n = kinfo(KINFO_PCI, 0, d, sizeof d);
    if (n < 0)
        return throw_errno(ctx, "kinfo");
    JSValue a = JS_NewArray(ctx);
    for (int i = 0; i < n / (int)sizeof d[0]; i++) {
        JSValue o = JS_NewObject(ctx);
        SETU(o, "bus", d[i].bus); SETU(o, "dev", d[i].dev); SETU(o, "fn", d[i].fn);
        SETU(o, "vendor", d[i].vendor); SETU(o, "device", d[i].device);
        SETU(o, "class", d[i].cls); SETU(o, "subclass", d[i].subclass);
        SETU(o, "progif", d[i].progif); SETU(o, "rev", d[i].rev); SETU(o, "irq", d[i].irq);
        JSValue bars = JS_NewArray(ctx);
        for (int b = 0; b < 6; b++)
            JS_SetPropertyUint32(ctx, bars, (uint32_t)b, JS_NewUint32(ctx, d[i].bar[b]));
        JS_SetPropertyStr(ctx, o, "bars", bars);
        JS_SetPropertyUint32(ctx, a, (uint32_t)i, o);
    }
    return a;
}

static JSValue k_proc(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct kiwi_procdetail d;
    if (kinfo(KINFO_PROC, argc > 0 ? (int)u32(ctx, argv[0]) : 0, &d, sizeof d) < 0)
        return throw_errno(ctx, "kinfo");
    static const char *states[] = { "unused", "ready", "blocked", "zombie" };
    static const char *ftypes[] = { "", "console", "file", "dir", "pipe<", "pipe>", "device" };
    JSValue o = JS_NewObject(ctx);
    SETU(o, "pid", d.pid); SETU(o, "ppid", d.ppid); SETU(o, "pgid", d.pgid);
    JS_SetPropertyStr(ctx, o, "state", JS_NewString(ctx, states[d.state & 3]));
    JS_SetPropertyStr(ctx, o, "name", JS_NewString(ctx, d.name));
    JS_SetPropertyStr(ctx, o, "cwd", JS_NewString(ctx, d.cwd));
    SETU(o, "kstack", d.kstack); SETU(o, "kesp", d.kesp); SETU(o, "pageDir", d.pd);
    SETU(o, "heapStart", d.heap_start); SETU(o, "brk", d.brk); SETU(o, "mmapNext", d.mmap_next);
    SETU(o, "resident", d.resident); SETU(o, "compressed", d.compressed); SETU(o, "ticks", d.ticks);
    SETU(o, "killed", d.killed); SETU(o, "iopl", d.iopl);
    JSValue vmas = JS_NewArray(ctx);
    for (uint32_t i = 0; i < d.nvma && i < 8; i++) {
        JSValue v = JS_NewObject(ctx);
        SETU(v, "start", d.vma[i].start); SETU(v, "end", d.vma[i].end);
        JS_SetPropertyStr(ctx, v, "type", JS_NewString(ctx, d.vma[i].type == 2 ? "device" : "anon"));
        JS_SetPropertyUint32(ctx, vmas, i, v);
    }
    JS_SetPropertyStr(ctx, o, "vmas", vmas);
    JSValue fds = JS_NewArray(ctx);
    for (uint32_t i = 0; i < 16; i++)
        JS_SetPropertyUint32(ctx, fds, i, JS_NewString(ctx, ftypes[d.fd_type[i] < 7 ? d.fd_type[i] : 0]));
    JS_SetPropertyStr(ctx, o, "fds", fds);
    return o;
}

static JSValue k_zram(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    struct kiwi_zraminfo z;
    if (kinfo(KINFO_ZRAM, 0, &z, sizeof z) < 0)
        return throw_errno(ctx, "kinfo");
    JSValue o = JS_NewObject(ctx);
    SETU(o, "slotsTotal", z.slots_total); SETU(o, "slotsUsed", z.slots_used);
    SETU(o, "maxRefs", z.max_refs); SETU(o, "sharedObjects", z.shared_objects);
    SETU(o, "budget", z.budget); SETU(o, "resident", z.resident);
    SETU(o, "clockHand", z.clock_hand); SETU(o, "idleHand", z.idle_hand);
    JSValue t = JS_NewObject(ctx);
    SETU(t, "same", z.by_type[1]); SETU(t, "lz", z.by_type[2]); SETU(t, "raw", z.by_type[3]);
    SETU(t, "lzh", z.by_type[4]);
    JS_SetPropertyStr(ctx, o, "byType", t);
    JSValue h = JS_NewArray(ctx);
    for (int i = 0; i < 17; i++)
        JS_SetPropertyUint32(ctx, h, (uint32_t)i, JS_NewUint32(ctx, z.size_hist[i]));
    JS_SetPropertyStr(ctx, o, "sizeHistogram", h);    /* 256-byte buckets */
    return o;
}

static JSValue k_peek(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    uint32_t phys = u32(ctx, argv[0]), len = argc > 1 ? u32(ctx, argv[1]) : 256;
    if (len > 65536)
        len = 65536;
    void *b = malloc(len ? len : 1);
    if (!b)
        return JS_ThrowOutOfMemory(ctx);
    if (kiwi_peek(phys, b, len) < 0) {
        free(b);
        return throw_errno(ctx, "peek");
    }
    JSValue v = JS_NewArrayBufferCopy(ctx, b, len);
    free(b);
    return v;
}

static JSValue k_peek32(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv)
{
    uint32_t v;
    if (kiwi_peek(u32(ctx, argv[0]), &v, 4) < 0)
        return throw_errno(ctx, "peek");
    return JS_NewUint32(ctx, v);
}

static const JSCFunctionListEntry kernel_funcs[] = {
    JS_CFUNC_DEF("inb", 1, k_inb), JS_CFUNC_DEF("inw", 1, k_inw), JS_CFUNC_DEF("inl", 1, k_inl),
    JS_CFUNC_DEF("outb", 2, k_outb), JS_CFUNC_DEF("outw", 2, k_outw), JS_CFUNC_DEF("outl", 2, k_outl),
    JS_CFUNC_DEF("iopl", 1, k_iopl),
    JS_CFUNC_DEF("cpuid", 2, k_cpuid),
    JS_CFUNC_DEF("rdtsc", 0, k_rdtsc),
    JS_CFUNC_DEF("eflags", 0, k_eflags),
    JS_CFUNC_DEF("cpu", 0, k_cpu),
    JS_CFUNC_DEF("system", 0, k_system),
    JS_CFUNC_DEF("gdt", 0, k_gdt),
    JS_CFUNC_DEF("idt", 0, k_idt),
    JS_CFUNC_DEF("irqs", 0, k_irqs),
    JS_CFUNC_DEF("memoryMap", 0, k_memmap),
    JS_CFUNC_DEF("pagemap", 1, k_pagemap),
    JS_CFUNC_DEF("dmesg", 0, k_dmesg),
    JS_CFUNC_DEF("pci", 0, k_pci),
    JS_CFUNC_DEF("proc", 1, k_proc),
    JS_CFUNC_DEF("zram", 0, k_zram),
    JS_CFUNC_DEF("font", 0, k_font),
    JS_CFUNC_DEF("peek", 2, k_peek),
    JS_CFUNC_DEF("peek32", 1, k_peek32),
    JS_PROP_INT32_DEF("PTE_PRESENT", KPTE_PRESENT, 0),
    JS_PROP_INT32_DEF("PTE_WRITE", KPTE_WRITE, 0),
    JS_PROP_INT32_DEF("PTE_USER", KPTE_USER, 0),
    JS_PROP_INT32_DEF("PTE_ACCESSED", KPTE_ACCESSED, 0),
    JS_PROP_INT32_DEF("PTE_DIRTY", KPTE_DIRTY, 0),
    JS_PROP_INT32_DEF("PTE_ZRAM", KPTE_ZRAM, 0),
    JS_PROP_INT32_DEF("PTE_PHYS", KPTE_PHYS, 0),
};

static int kernel_init(JSContext *ctx, JSModuleDef *m)
{
    return JS_SetModuleExportList(ctx, m, kernel_funcs, sizeof kernel_funcs / sizeof kernel_funcs[0]);
}

JSModuleDef *kjs_module_kernel(JSContext *ctx, const char *name)
{
    JSModuleDef *m = JS_NewCModule(ctx, name, kernel_init);
    if (m)
        JS_AddModuleExportList(ctx, m, kernel_funcs, sizeof kernel_funcs / sizeof kernel_funcs[0]);
    return m;
}
#endif
