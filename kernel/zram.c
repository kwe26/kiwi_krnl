/*
 * zram: compressed memory for user pages.
 *
 * Every user page frame is tracked in a reverse map (frame -> owner, pd,
 * va).  The number of resident user pages is capped by a budget; when a
 * new frame is needed beyond the budget, a clock sweep over the reverse
 * map picks a page whose accessed bit is clear and compresses it into the
 * pool.  The PTE then holds a slot number (PTE_Z) and the next touch faults
 * the page back in.
 *
 * Pool objects are:
 *   SAME  pages filled with one repeated 32-bit word (zero pages...) - no data
 *   LZ    KLZ-compressed bytes (fast path: allocating under memory pressure)
 *   LZH   KLZ + Huffman (heavy path: the idle thread has CPU to spare)
 *   RAW   incompressible pages kept verbatim
 * and are deduplicated by content hash with reference counts, so identical
 * pages (e.g. the same binary running twice, or fork) are stored once.
 *
 * The idle thread also compresses pages that stayed untouched for
 * `zram_proactive` scans: memory of sleeping processes shrinks on its own.
 */
#include "kernel.h"
#include "lz.h"

enum { Z_FREE, Z_SAME, Z_LZ, Z_RAW, Z_LZH };

struct zslot {
    uint8_t *data;
    uint32_t pattern;
    uint32_t refs;
    uint32_t hash;
    int32_t  hnext;         /* hash chain, or free-list link */
    uint16_t len;
    uint8_t  type;
};

struct uframe {
    uint32_t pd;
    uint32_t va;
    int16_t  owner;         /* proc index */
    uint8_t  age;
    uint8_t  used;
};

#define NBUCKET 4096
#define RAW_THRESHOLD 3584      /* LZ output above this is stored raw */

static struct uframe *ft;
static struct zslot *slots;
static uint32_t nslots, slot_free = 0xFFFFFFFFu;
static int32_t buckets[NBUCKET];
static uint32_t hand, idle_hand;
static uint8_t ztmp[LZ_BOUND(PAGE_SIZE)], ztmp2[LZ_BOUND(PAGE_SIZE)];

uint32_t zram_budget, zram_proactive = 3;
static uint32_t user_resident;
static struct {
    uint32_t pages, uniq, same, lz, lzh, raw, bytes, dedup, outs, ins;
} zs;

void zram_init(void)
{
    ft = kzalloc(pmm_max_frame * sizeof *ft);
    if (!ft)
        panic("zram: no memory for frame table");
    for (int i = 0; i < NBUCKET; i++)
        buckets[i] = -1;
    nslots = 1024;
    slots = kzalloc(nslots * sizeof *slots);
    for (uint32_t i = nslots; i-- > 1; ) {     /* slot 0 is never used */
        slots[i].hnext = (int32_t)slot_free;
        slot_free = i;
    }
    zram_set_budget(0);
}

static uint32_t slot_new(void)
{
    if (slot_free == 0xFFFFFFFFu) {
        uint32_t n = nslots * 2;
        if (n > (1u << 20))
            return 0;
        struct zslot *s = krealloc(slots, n * sizeof *s);
        if (!s)
            return 0;
        memset(s + nslots, 0, (n - nslots) * sizeof *s);
        slots = s;
        for (uint32_t i = n; i-- > nslots; ) {
            slots[i].hnext = (int32_t)slot_free;
            slot_free = i;
        }
        nslots = n;
    }
    uint32_t i = slot_free;
    slot_free = (uint32_t)slots[i].hnext;
    return i;
}

static void count(int type, int d)
{
    zs.pages += d;
    if (type == Z_SAME) zs.same += d;
    else if (type == Z_LZ) zs.lz += d;
    else if (type == Z_LZH) zs.lzh += d;
    else zs.raw += d;
}

static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

/* store one page, returns slot or 0 on failure */
static uint32_t zram_store(const uint8_t *page, int heavy)
{
    const uint32_t *w = (const uint32_t *)page;
    uint32_t i;
    for (i = 1; i < PAGE_SIZE / 4 && w[i] == w[0]; i++)
        ;
    if (i == PAGE_SIZE / 4) {
        uint32_t s = slot_new();
        if (!s) return 0;
        slots[s] = (struct zslot){ .type = Z_SAME, .pattern = w[0], .refs = 1, .hnext = -1 };
        zs.uniq++;
        count(Z_SAME, 1);
        return s;
    }

    size_t clen;
    int type = Z_LZ;
    const uint8_t *src = ztmp;
    if (heavy) {
        int method;
        clen = lz_pack(page, PAGE_SIZE, ztmp, sizeof ztmp, ztmp2, 16, &method);
        type = method == PACK_LZH ? Z_LZH : method == PACK_LZ ? Z_LZ : Z_RAW;
    } else {
        clen = lz_compress(page, PAGE_SIZE, ztmp, sizeof ztmp, LZ_LEVEL_FAST);
    }
    if (clen == 0 || clen > RAW_THRESHOLD || type == Z_RAW) {
        type = Z_RAW;
        src = page;
        clen = PAGE_SIZE;
    }
    uint32_t h = fnv(src, (uint32_t)clen);
    for (int32_t j = buckets[h % NBUCKET]; j >= 0; j = slots[j].hnext) {
        struct zslot *z = &slots[j];
        if (z->hash == h && z->type == type && z->len == (clen & 0xFFFF) &&
            memcmp(z->data, src, clen) == 0) {
            z->refs++;
            zs.dedup++;
            count(type, 1);
            return (uint32_t)j;
        }
    }
    uint8_t *d = kmalloc(clen);
    if (!d)
        return 0;
    memcpy(d, src, clen);
    uint32_t s = slot_new();
    if (!s) {
        kfree(d);
        return 0;
    }
    slots[s] = (struct zslot){ .data = d, .len = (uint16_t)clen, .type = (uint8_t)type,
                               .refs = 1, .hash = h, .hnext = buckets[h % NBUCKET] };
    buckets[h % NBUCKET] = (int32_t)s;
    zs.uniq++;
    zs.bytes += (uint32_t)clen;
    count(type, 1);
    return s;
}

static void zram_load(uint32_t s, uint8_t *dst)
{
    struct zslot *z = &slots[s];
    if (z->type == Z_SAME) {
        uint32_t *w = (uint32_t *)dst;
        for (uint32_t i = 0; i < PAGE_SIZE / 4; i++)
            w[i] = z->pattern;
    } else if (z->type == Z_RAW) {
        memcpy(dst, z->data, PAGE_SIZE);
    } else if (lz_unpack(z->data, z->len, dst, PAGE_SIZE, ztmp2, sizeof ztmp2,
                         z->type == Z_LZH ? PACK_LZH : PACK_LZ) != PAGE_SIZE) {
        panic("zram: corrupt slot %d", s);
    }
}

void zram_slot_dup(uint32_t s)
{
    slots[s].refs++;
    count(slots[s].type, 1);
}

void zram_slot_free(uint32_t s)
{
    struct zslot *z = &slots[s];
    if (s == 0 || s >= nslots || z->type == Z_FREE)
        panic("zram: bad slot free %d", s);
    count(z->type, -1);
    if (--z->refs)
        return;
    if (z->type != Z_SAME) {
        int32_t *pp = &buckets[z->hash % NBUCKET];
        while (*pp != (int32_t)s)
            pp = &slots[*pp].hnext;
        *pp = z->hnext;
        zs.bytes -= z->len;
        kfree(z->data);
    }
    zs.uniq--;
    z->type = Z_FREE;
    z->data = 0;
    z->hnext = (int32_t)slot_free;
    slot_free = s;
}

/* compress frame f out to the pool */
static int page_out(uint32_t f, int heavy)
{
    struct uframe *u = &ft[f];
    uint32_t pa = f * PAGE_SIZE;
    uint32_t *pte = vmm_pte(u->pd, u->va, 0);
    if (!pte || !(*pte & PTE_P) || (*pte & PAGE_MASK) != pa)
        panic("zram: stale reverse map for frame %x", pa);
    uint32_t s = zram_store((const uint8_t *)pa, heavy);
    if (!s)
        return -1;
    *pte = (s << 12) | PTE_Z | (*pte & (PTE_U | PTE_W));
    if (u->pd == read_cr3())
        invlpg(u->va);
    struct proc *p = &procs[u->owner];
    p->resident--;
    p->compressed++;
    u->used = 0;
    user_resident--;
    zs.outs++;
    pmm_free(pa);
    return 0;
}

/* clock: evict one page, giving recently used pages a second chance */
static int evict_one(void)
{
    uint32_t cr3 = read_cr3();
    for (uint32_t n = 0; n < 2 * pmm_max_frame + 1; n++) {
        uint32_t f = hand++;
        if (hand >= pmm_max_frame)
            hand = 0;
        struct uframe *u = &ft[f];
        if (!u->used)
            continue;
        uint32_t *pte = vmm_pte(u->pd, u->va, 0);
        if (*pte & PTE_A) {
            *pte &= ~PTE_A;
            if (u->pd == cr3)
                invlpg(u->va);
            continue;
        }
        if (page_out(f, 0) == 0)
            return 0;
    }
    return -1;
}

uint32_t kframe_alloc(void)
{
    uint32_t pa = pmm_alloc();
    if (!pa && ft && evict_one() == 0)
        pa = pmm_alloc();
    return pa;
}

uint32_t uframe_alloc(struct proc *owner, uint32_t pd, uint32_t va)
{
    while (user_resident >= zram_budget)
        if (evict_one() < 0)
            break;
    uint32_t pa = pmm_alloc();
    while (!pa && evict_one() == 0)
        pa = pmm_alloc();
    if (!pa)
        return 0;
    struct uframe *u = &ft[pa / PAGE_SIZE];
    u->pd = pd;
    u->va = va & PAGE_MASK;
    u->owner = (int16_t)(owner - procs);
    u->age = 0;
    u->used = 1;
    owner->resident++;
    user_resident++;
    return pa;
}

void uframe_release(uint32_t pa)
{
    struct uframe *u = &ft[pa / PAGE_SIZE];
    if (!u->used)
        panic("zram: releasing untracked frame %x", pa);
    procs[u->owner].resident--;
    u->used = 0;
    user_resident--;
    pmm_free(pa);
}

int zram_page_in(struct proc *owner, uint32_t pd, uint32_t va, uint32_t *pte)
{
    uint32_t s = *pte >> 12;
    uint32_t flags = *pte & (PTE_U | PTE_W);
    uint32_t pa = uframe_alloc(owner, pd, va);
    if (!pa)
        return -1;
    zram_load(s, (uint8_t *)pa);
    *pte = pa | PTE_P | PTE_A | flags;
    zram_slot_free(s);
    owner->compressed--;
    zs.ins++;
    return 0;
}

/* called from the idle loop: age pages, compress the cold ones */
void zram_idle_scan(void)
{
    if (!zram_proactive || !ft)
        return;
    for (int n = 0; n < 512; n++) {
        uint32_t f = idle_hand++;
        if (idle_hand >= pmm_max_frame)
            idle_hand = 0;
        struct uframe *u = &ft[f];
        if (!u->used)
            continue;
        uint32_t *pte = vmm_pte(u->pd, u->va, 0);
        if (*pte & PTE_A) {
            *pte &= ~PTE_A;     /* idle runs on kernel_pd: no TLB entry */
            u->age = 0;
        } else if (++u->age >= zram_proactive) {
            page_out(f, 1);
        }
    }
}

int zram_compress_all(void)
{
    int n = 0;
    for (uint32_t f = 0; f < pmm_max_frame; f++) {
        if (ft[f].used && &procs[ft[f].owner] != current && page_out(f, 1) == 0)
            n++;
    }
    return n;
}

void zram_set_budget(uint32_t pages)
{
    if (pages == 0)
        pages = (pmm_free_frames + user_resident) * 3 / 4;
    if (pages < 256)
        pages = 256;
    zram_budget = pages;
    while (user_resident > zram_budget && evict_one() == 0)
        ;
}

void zram_info(struct kiwi_zraminfo *z)
{
    memset(z, 0, sizeof *z);
    z->slots_total = nslots;
    for (uint32_t i = 1; i < nslots; i++) {
        struct zslot *s = &slots[i];
        z->by_type[s->type < 5 ? s->type : 0]++;
        if (s->type == Z_FREE)
            continue;
        z->slots_used++;
        uint32_t b = s->type == Z_SAME ? 0 : s->len / 256u;
        z->size_hist[b < 17 ? b : 16]++;
        if (s->refs > z->max_refs)
            z->max_refs = s->refs;
        if (s->refs > 1)
            z->shared_objects++;
    }
    z->budget = zram_budget;
    z->resident = user_resident;
    z->clock_hand = hand;
    z->idle_hand = idle_hand;
}

void zram_stat(struct kiwi_kzstat *st)
{
    memset(st, 0, sizeof *st);
    st->page_size = PAGE_SIZE;
    st->total_frames = pmm_total_frames;
    st->free_frames = pmm_free_frames;
    st->user_resident = user_resident;
    st->budget = zram_budget;
    st->proactive = zram_proactive;
    st->z_pages = zs.pages;
    st->z_slots = zs.uniq;
    st->z_same = zs.same;
    st->z_lz = zs.lz;
    st->z_lzh = zs.lzh;
    st->z_raw = zs.raw;
    st->z_bytes = zs.bytes + zs.uniq * (uint32_t)sizeof(struct zslot);
    st->z_dedup = zs.dedup;
    st->z_pageouts = zs.outs;
    st->z_pageins = zs.ins;
    ramfs_totals(&st->fs_files, &st->fs_bytes, &st->fs_zbytes);
    heap_stats(&st->heap_total, &st->heap_used);
}
