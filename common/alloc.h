/*
 * Binned boundary-tag allocator shared by the kernel heap and the
 * userland malloc.  The owner supplies a morecore() that grows the arena
 * contiguously (a fixed region in the kernel, sbrk() in userland).
 */
#ifndef KIWI_ALLOC_H
#define KIWI_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#define ALLOC_NBINS 152

struct achunk;

struct aheap {
    struct achunk *bins[ALLOC_NBINS];
    uint32_t binmap[(ALLOC_NBINS + 31) / 32];
    struct achunk *top;
    char *start, *end;
    /* grow the arena by at least incr bytes at 'end'; returns 0 on failure */
    void *(*morecore)(struct aheap *h, size_t incr);
    size_t in_use;          /* bytes handed out (chunk sizes) */
    size_t peak;
};

void   aheap_init(struct aheap *h, void *start, size_t size,
                  void *(*morecore)(struct aheap *, size_t));
void  *aheap_alloc(struct aheap *h, size_t n);
void   aheap_free(struct aheap *h, void *p);
void  *aheap_realloc(struct aheap *h, void *p, size_t n);
size_t aheap_usable(const void *p);

#endif
