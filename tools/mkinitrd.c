/*
 * mkinitrd: pack a directory tree into a Kiwi initrd image.  Every file is
 * packed with the best of raw / KLZ / KLZ+Huffman so the kernel can keep it
 * compressed in RAM.
 *
 *   "KIRD" u32 count
 *   { u8 type(1 file, 2 dir) u8 method(0 LZ, 1 raw, 2 LZ+Huffman) u16 pathlen u32 size u32 csize
 *     path[pathlen] data[csize] } * count
 */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lz.h"

static FILE *out;
static uint32_t count, total_raw, total_packed, methods[3];

static void w8(uint8_t v) { fputc(v, out); }
static void w16(uint16_t v) { w8(v & 0xff); w8(v >> 8); }
static void w32(uint32_t v) { w16(v & 0xffff); w16(v >> 16); }

static void entry(int type, const char *path, const uint8_t *data, uint32_t size)
{
    uint8_t *packed = 0;
    uint32_t csize = 0;
    int method = PACK_RAW;
    if (size) {
        packed = malloc(LZ_BOUND(size));
        uint8_t *tmp = malloc(LZ_BOUND(size)), *chk = malloc(size);
        csize = (uint32_t)lz_pack(data, size, packed, LZ_BOUND(size), tmp, LZ_LEVEL_HEAVY, &method);
        /* verify the round trip before trusting the compressor */
        if (!csize || lz_unpack(packed, csize, chk, size, tmp, LZ_BOUND(size), method) != (long)size ||
            memcmp(chk, data, size) != 0) {
            fprintf(stderr, "mkinitrd: round-trip failed for %s\n", path);
            exit(1);
        }
        free(tmp);
        free(chk);
        methods[method]++;
    }
    w8((uint8_t)type);
    w8((uint8_t)method);
    w16((uint16_t)strlen(path));
    w32(size);
    w32(csize);
    fwrite(path, 1, strlen(path), out);
    if (size)
        fwrite(packed, 1, csize, out);
    free(packed);
    count++;
    total_raw += size;
    total_packed += csize;
}

static int cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void walk(const char *host, const char *path)
{
    DIR *d = opendir(host);
    if (!d) {
        perror(host);
        exit(1);
    }
    char *names[1024];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < 1024) {
        if (de->d_name[0] == '.')
            continue;
        names[n++] = strdup(de->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp);
    for (int i = 0; i < n; i++) {
        char hp[1024], kp[512];
        snprintf(hp, sizeof hp, "%s/%s", host, names[i]);
        snprintf(kp, sizeof kp, "%s/%s", path, names[i]);
        struct stat st;
        if (stat(hp, &st) < 0) {
            perror(hp);
            exit(1);
        }
        if (S_ISDIR(st.st_mode)) {
            entry(2, kp, 0, 0);
            walk(hp, kp);
        } else if (S_ISREG(st.st_mode)) {
            FILE *f = fopen(hp, "rb");
            uint8_t *buf = malloc(st.st_size ? st.st_size : 1);
            if (!f || fread(buf, 1, st.st_size, f) != (size_t)st.st_size) {
                perror(hp);
                exit(1);
            }
            fclose(f);
            entry(1, kp, buf, (uint32_t)st.st_size);
            free(buf);
        }
        free(names[i]);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: mkinitrd <rootdir> <image>\n");
        return 1;
    }
    out = fopen(argv[2], "wb");
    if (!out) {
        perror(argv[2]);
        return 1;
    }
    fwrite("KIRD", 1, 4, out);
    w32(0);
    walk(argv[1], "");
    fseek(out, 4, SEEK_SET);
    w32(count);
    fclose(out);
    printf("mkinitrd: %u entries, %u KB -> %u KB (%.1f%%); files: %u lz+huffman, %u lz, %u raw\n",
           count, total_raw / 1024, total_packed / 1024,
           total_raw ? 100.0 * total_packed / total_raw : 0.0,
           methods[PACK_LZH], methods[PACK_LZ], methods[PACK_RAW]);
    return 0;
}
