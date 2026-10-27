/*
 * KLZ - the Kiwi LZ77 compressor.
 *
 * Shared by the kernel (zram page store, ramfs) and the host tools
 * (mkinitrd).  Block format is LZ4-like:
 *
 *   token  : [literal len:4][match len - 4:4]   (15 = more bytes follow)
 *   literals
 *   offset : u16 little endian (only if more input follows)
 *   extra match length bytes
 *
 * The last sequence carries literals only.
 */
#ifndef KIWI_LZ_H
#define KIWI_LZ_H

#include <stddef.h>
#include <stdint.h>

#define LZ_LEVEL_FAST   1   /* zram page-out: single hash probe      */
#define LZ_LEVEL_HEAVY  64  /* files: deep hash chains + lazy match   */

/* worst-case output size for n input bytes */
#define LZ_BOUND(n) ((n) + (n) / 255 + 16)

/* returns compressed size, or 0 if it does not fit in cap */
size_t lz_compress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap,
                   int level);

/* returns decompressed size, or -1 on corrupt input / overflow */
long lz_decompress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap);

/*
 * Packing with method selection (used for files and idle-time page-out):
 * the smallest of raw / LZ / LZ+Huffman.  tmp must hold LZ_BOUND(n).
 */
enum { PACK_LZ = 0, PACK_RAW = 1, PACK_LZH = 2 };
size_t lz_pack(const uint8_t *src, size_t n, uint8_t *dst, size_t cap,
               uint8_t *tmp, int level, int *method);
/* tmp must hold the intermediate LZ stream (<= LZ_BOUND(out_size)) */
long   lz_unpack(const uint8_t *src, size_t n, uint8_t *dst, size_t cap,
                 uint8_t *tmp, size_t tmpcap, int method);

#endif
