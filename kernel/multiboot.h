#ifndef KIWI_MULTIBOOT_H
#define KIWI_MULTIBOOT_H

#include <stdint.h>

#define MB_BOOT_MAGIC  0x2BADB002
#define MB_INFO_MEM    0x001
#define MB_INFO_CMDLINE 0x004
#define MB_INFO_MODS   0x008
#define MB_INFO_MMAP   0x040

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower, mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr;
} __attribute__((packed));

struct mb_module {
    uint32_t start, end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed));

struct mb_mmap {
    uint32_t size;
    uint64_t addr, len;
    uint32_t type;
} __attribute__((packed));

#endif
