/*
 * memhog: stress the zram compressed page store.
 *
 *   memhog [MB] [pattern]
 *     pattern: text (default, compressible), zero, random (incompressible)
 *
 * Allocates MB megabytes, fills them, then verifies every byte while the
 * kernel compresses and decompresses pages behind our back.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kiwi/kiwi.h>

static void report(const char *when)
{
    struct kiwi_kzstat z;
    kz_stat(&z);
    unsigned orig = z.z_pages * 4;
    printf("%-10s resident %5u KB  zram %5u KB -> %5u KB  out %u in %u dedup %u\n",
           when, z.user_resident * 4, orig, z.z_bytes / 1024,
           z.z_pageouts, z.z_pageins, z.z_dedup);
}

int main(int argc, char **argv)
{
    int mb = argc > 1 ? atoi(argv[1]) : 16;
    const char *pat = argc > 2 ? argv[2] : "text";
    size_t n = (size_t)mb << 20;
    unsigned seed = 12345;
    static const char words[] = "kiwi compresses memory pages with lz77 ";

    report("start");
    unsigned char *p = malloc(n);
    if (!p) {
        printf("memhog: cannot allocate %d MB\n", mb);
        return 1;
    }
    unsigned t0 = kiwi_uptime_ms();
    for (size_t i = 0; i < n; i++) {
        if (pat[0] == 'z') p[i] = 0;
        else if (pat[0] == 'r') { seed = seed * 1103515245u + 12345u; p[i] = (unsigned char)(seed >> 16); }
        else p[i] = (unsigned char)words[(i + i / 4096) % (sizeof words - 1)];
    }
    report("filled");
    seed = 12345;
    size_t bad = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char want;
        if (pat[0] == 'z') want = 0;
        else if (pat[0] == 'r') { seed = seed * 1103515245u + 12345u; want = (unsigned char)(seed >> 16); }
        else want = (unsigned char)words[(i + i / 4096) % (sizeof words - 1)];
        if (p[i] != want)
            bad++;
    }
    report("verified");
    printf("memhog: %d MB '%s' %s in %u ms\n", mb, pat, bad ? "CORRUPTED" : "OK",
           kiwi_uptime_ms() - t0);
    return bad != 0;
}
