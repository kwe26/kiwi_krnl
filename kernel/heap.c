/* Kernel heap: a fixed identity-mapped region managed by common/alloc.c */
#include "kernel.h"
#include "alloc.h"

static struct aheap kheap;
static uint32_t heap_size;
uint32_t kheap_start, kheap_size;

void heap_init(uint32_t start, uint32_t size)
{
    heap_size = size;
    kheap_start = start;
    kheap_size = size;
    aheap_init(&kheap, (void *)start, size, 0);
}

void *kmalloc(size_t n)
{
    return aheap_alloc(&kheap, n);
}

void *kzalloc(size_t n)
{
    void *p = aheap_alloc(&kheap, n);
    if (p)
        memset(p, 0, n);
    return p;
}

void *krealloc(void *p, size_t n)
{
    return aheap_realloc(&kheap, p, n);
}

void kfree(void *p)
{
    aheap_free(&kheap, p);
}

char *kstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = kmalloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

void heap_stats(uint32_t *total, uint32_t *used)
{
    *total = heap_size;
    *used = (uint32_t)kheap.in_use;
}
