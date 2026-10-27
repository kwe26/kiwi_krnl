/* Physical frame allocator: one bit per 4KB frame. */
#include "kernel.h"
#include "multiboot.h"

#define MAX_FRAMES (1024u * 1024u * 1024u / PAGE_SIZE)   /* 1GB cap */

static uint32_t bitmap[MAX_FRAMES / 32];
uint32_t pmm_total_frames, pmm_free_frames, pmm_max_frame;
static uint32_t hint;
struct kiwi_memrange boot_mmap[32];
int boot_mmap_n;

static inline int used(uint32_t f) { return bitmap[f >> 5] & (1u << (f & 31)); }
static inline void set_used(uint32_t f) { bitmap[f >> 5] |= 1u << (f & 31); }
static inline void set_free(uint32_t f) { bitmap[f >> 5] &= ~(1u << (f & 31)); }

void pmm_release(uint32_t start, uint32_t end)
{
    for (uint32_t f = ALIGN_UP(start, PAGE_SIZE) / PAGE_SIZE; f < end / PAGE_SIZE && f < pmm_max_frame; f++) {
        if (used(f)) {
            set_free(f);
            pmm_free_frames++;
        }
    }
}

void pmm_reserve(uint32_t start, uint32_t end)
{
    for (uint32_t f = start / PAGE_SIZE; f < ALIGN_UP(end, PAGE_SIZE) / PAGE_SIZE && f < pmm_max_frame; f++) {
        if (!used(f)) {
            set_used(f);
            pmm_free_frames--;
        }
    }
}

void pmm_init(struct multiboot_info *mbi, uint32_t reserve_end)
{
    memset(bitmap, 0xFF, sizeof bitmap);
    pmm_free_frames = 0;
    uint64_t top = 0;

    if (mbi->flags & MB_INFO_MMAP) {
        uint32_t p = mbi->mmap_addr;
        while (p < mbi->mmap_addr + mbi->mmap_length) {
            struct mb_mmap *m = (struct mb_mmap *)p;
            if (m->type == 1 && m->addr < 0x40000000ull) {
                uint64_t end = m->addr + m->len;
                if (end > 0x40000000ull) end = 0x40000000ull;
                if (end > top) top = end;
            }
            p += m->size + 4;
        }
        pmm_max_frame = (uint32_t)(top / PAGE_SIZE);
        for (p = mbi->mmap_addr; p < mbi->mmap_addr + mbi->mmap_length && boot_mmap_n < 32; ) {
            struct mb_mmap *m = (struct mb_mmap *)p;
            boot_mmap[boot_mmap_n++] = (struct kiwi_memrange){ m->addr, m->len, m->type, 0 };
            p += m->size + 4;
        }
        p = mbi->mmap_addr;
        while (p < mbi->mmap_addr + mbi->mmap_length) {
            struct mb_mmap *m = (struct mb_mmap *)p;
            if (m->type == 1 && m->addr < 0x40000000ull) {
                uint64_t end = m->addr + m->len;
                if (end > 0x40000000ull) end = 0x40000000ull;
                pmm_release((uint32_t)m->addr, (uint32_t)end);
            }
            p += m->size + 4;
        }
    } else {
        top = (uint64_t)(mbi->mem_upper + 1024) * 1024;
        pmm_max_frame = (uint32_t)(top / PAGE_SIZE);
        pmm_release(0x100000, (uint32_t)top);
    }
    pmm_total_frames = pmm_free_frames;
    pmm_reserve(0, 0x100000);               /* BIOS, VGA, real mode area */
    pmm_reserve(0x100000, reserve_end);     /* kernel image + boot modules */
    hint = reserve_end / PAGE_SIZE;
}

uint32_t pmm_alloc(void)
{
    if (!pmm_free_frames)
        return 0;
    for (uint32_t n = 0; n < pmm_max_frame; n++) {
        uint32_t f = hint + n;
        if (f >= pmm_max_frame)
            f -= pmm_max_frame;
        if (!(bitmap[f >> 5] + 1) ) {     /* whole word used: skip ahead */
            n += 31 - (f & 31);
            continue;
        }
        if (!used(f)) {
            set_used(f);
            pmm_free_frames--;
            hint = f + 1;
            return f * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free(uint32_t pa)
{
    uint32_t f = pa / PAGE_SIZE;
    if (f >= pmm_max_frame || !used(f))
        panic("pmm_free: bad frame %x", pa);
    set_free(f);
    pmm_free_frames++;
}
