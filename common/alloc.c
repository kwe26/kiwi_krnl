/*
 * Boundary-tag allocator with exact small bins and log2 large bins.
 *
 * chunk layout (all sizes multiple of 8, chunk addresses 8-aligned):
 *
 *   +0  prev_size   only meaningful when the previous chunk is free
 *   +4  size | INUSE | PINUSE
 *   +8  payload ... (free chunks keep fd/bk here)
 *
 * An in-use chunk's payload overlaps the next chunk's prev_size field,
 * so the usable size is CSIZE - 4.
 */
#include "alloc.h"

struct achunk {
    size_t prev_size;
    size_t size;
    struct achunk *fd, *bk;
};

#define INUSE   1u
#define PINUSE  2u
#define FLAGS   7u
#define MINCHUNK 16u
#define SMALL_LIMIT 1024u

#define CSIZE(c)   ((c)->size & ~FLAGS)
#define NEXT(c)    ((struct achunk *)((char *)(c) + CSIZE(c)))
#define PREV(c)    ((struct achunk *)((char *)(c) - (c)->prev_size))
#define MEM(c)     ((void *)((char *)(c) + 8))
#define CHUNK(p)   ((struct achunk *)((char *)(p) - 8))

static size_t req2size(size_t n)
{
    size_t s = (n + 4 + 7) & ~(size_t)7;
    return s < MINCHUNK ? MINCHUNK : s;
}

static unsigned bin_index(size_t sz)
{
    if (sz < SMALL_LIMIT)
        return (unsigned)(sz >> 3);             /* 2..127 */
    unsigned lg = 31 - (unsigned)__builtin_clz((unsigned)sz);   /* >= 10 */
    unsigned i = 128 + (lg - 10) * 2 + ((sz >> (lg - 1)) & 1);
    return i < ALLOC_NBINS ? i : ALLOC_NBINS - 1;
}

static void bin_insert(struct aheap *h, struct achunk *c)
{
    unsigned i = bin_index(CSIZE(c));
    c->bk = 0;
    c->fd = h->bins[i];
    if (c->fd)
        c->fd->bk = c;
    h->bins[i] = c;
    h->binmap[i >> 5] |= 1u << (i & 31);
}

static void bin_remove(struct aheap *h, struct achunk *c)
{
    unsigned i = bin_index(CSIZE(c));
    if (c->bk)
        c->bk->fd = c->fd;
    else
        h->bins[i] = c->fd;
    if (c->fd)
        c->fd->bk = c->bk;
    if (!h->bins[i])
        h->binmap[i >> 5] &= ~(1u << (i & 31));
}

/* mark chunk c (size sz) free: write footer, clear next's PINUSE */
static void set_free(struct achunk *c, size_t sz)
{
    c->size = sz | (c->size & PINUSE);
    struct achunk *n = (struct achunk *)((char *)c + sz);
    n->prev_size = sz;
    n->size &= ~PINUSE;
}

void aheap_init(struct aheap *h, void *start, size_t size,
                void *(*morecore)(struct aheap *, size_t))
{
    for (unsigned i = 0; i < ALLOC_NBINS; i++)
        h->bins[i] = 0;
    for (unsigned i = 0; i < sizeof(h->binmap) / 4; i++)
        h->binmap[i] = 0;
    char *s = (char *)(((uintptr_t)start + 7) & ~(uintptr_t)7);
    size -= (size_t)(s - (char *)start);
    size &= ~(size_t)7;
    h->start = s;
    h->end = s + size;
    h->morecore = morecore;
    h->in_use = h->peak = 0;
    h->top = (struct achunk *)s;
    h->top->prev_size = 0;
    h->top->size = size | PINUSE;
}

static size_t top_size(struct aheap *h)
{
    return (size_t)(h->end - (char *)h->top);
}

/* split chunk c (free, unlinked) to sz bytes, returning the remainder */
static void *use_chunk(struct aheap *h, struct achunk *c, size_t sz)
{
    size_t csz = CSIZE(c);
    if (csz - sz >= MINCHUNK) {
        struct achunk *r = (struct achunk *)((char *)c + sz);
        r->size = (csz - sz) | PINUSE;
        set_free(r, csz - sz);
        bin_insert(h, r);
        csz = sz;
    } else {
        NEXT(c)->size |= PINUSE;
    }
    c->size = csz | INUSE | (c->size & PINUSE);
    h->in_use += csz;
    if (h->in_use > h->peak)
        h->peak = h->in_use;
    return MEM(c);
}

void *aheap_alloc(struct aheap *h, size_t n)
{
    if (n > 0x7ff00000u)
        return 0;
    size_t sz = req2size(n);
    unsigned i = bin_index(sz);

    /* exact/next small bin, then large bins via the bitmap */
    for (unsigned w = i >> 5; w < sizeof(h->binmap) / 4; w++) {
        uint32_t m = h->binmap[w];
        if (w == (i >> 5))
            m &= ~0u << (i & 31);
        while (m) {
            unsigned b = w * 32 + (unsigned)__builtin_ctz(m);
            m &= m - 1;
            for (struct achunk *c = h->bins[b]; c; c = c->fd) {
                if (CSIZE(c) >= sz) {
                    bin_remove(h, c);
                    return use_chunk(h, c, sz);
                }
            }
        }
    }

    /* carve from the top chunk, growing the arena if needed */
    if (top_size(h) < sz + MINCHUNK) {
        size_t need = sz + MINCHUNK - top_size(h);
        if (!h->morecore || !h->morecore(h, need))
            return 0;
    }
    struct achunk *c = h->top;
    size_t pin = c->size & PINUSE;
    h->top = (struct achunk *)((char *)c + sz);
    h->top->size = top_size(h) | PINUSE;
    c->size = sz | INUSE | pin;
    h->in_use += sz;
    if (h->in_use > h->peak)
        h->peak = h->in_use;
    return MEM(c);
}

void aheap_free(struct aheap *h, void *p)
{
    if (!p)
        return;
    struct achunk *c = CHUNK(p);
    size_t sz = CSIZE(c);
    h->in_use -= sz;

    if (!(c->size & PINUSE)) {
        struct achunk *pv = PREV(c);
        bin_remove(h, pv);
        sz += CSIZE(pv);
        c = pv;
    }
    struct achunk *nx = (struct achunk *)((char *)c + sz);
    if (nx == h->top) {
        size_t pin = c->size & PINUSE;
        h->top = c;
        c->size = top_size(h) | pin;
        return;
    }
    if (!(nx->size & INUSE)) {
        bin_remove(h, nx);
        sz += CSIZE(nx);
    }
    c->size = sz | (c->size & PINUSE);
    set_free(c, sz);
    bin_insert(h, c);
}

size_t aheap_usable(const void *p)
{
    return CSIZE(CHUNK(p)) - 4;
}

void *aheap_realloc(struct aheap *h, void *p, size_t n)
{
    if (!p)
        return aheap_alloc(h, n);
    if (n == 0) {
        aheap_free(h, p);
        return 0;
    }
    if (n > 0x7ff00000u)
        return 0;
    struct achunk *c = CHUNK(p);
    size_t sz = req2size(n), csz = CSIZE(c);
    if (csz >= sz)
        return p;
    struct achunk *nx = NEXT(c);
    if (nx == h->top) {
        if (top_size(h) < sz - csz + MINCHUNK) {
            if (!h->morecore || !h->morecore(h, sz - csz + MINCHUNK - top_size(h)))
                goto move;
        }
        c->size = sz | INUSE | (c->size & PINUSE);
        h->top = (struct achunk *)((char *)c + sz);
        h->top->size = top_size(h) | PINUSE;
        h->in_use += sz - csz;
        if (h->in_use > h->peak)
            h->peak = h->in_use;
        return p;
    }
    if (!(nx->size & INUSE) && csz + CSIZE(nx) >= sz) {
        bin_remove(h, nx);
        size_t tot = csz + CSIZE(nx);
        h->in_use -= csz;
        c->size = tot | INUSE | (c->size & PINUSE);
        NEXT(c)->size |= PINUSE;
        /* give back the tail if big enough */
        if (tot - sz >= MINCHUNK) {
            c->size = sz | INUSE | (c->size & PINUSE);
            struct achunk *r = (struct achunk *)((char *)c + sz);
            r->size = (tot - sz) | PINUSE | INUSE;
            h->in_use += sz + (tot - sz);
            aheap_free(h, MEM(r));
        } else {
            h->in_use += tot;
        }
        return p;
    }
move:;
    void *q = aheap_alloc(h, n);
    if (!q)
        return 0;
    size_t copy = csz - 4;
    char *d = q;
    const char *s = p;
    for (size_t i = 0; i < copy; i++)
        d[i] = s[i];
    aheap_free(h, p);
    return q;
}
