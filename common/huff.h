/*
 * Byte-wise canonical Huffman coder: the entropy stage of KLZ heavy mode.
 *
 *   u24 original size | 128 bytes of 4-bit code lengths | MSB-first bits
 */
#ifndef KIWI_HUFF_H
#define KIWI_HUFF_H

#include <stddef.h>
#include <stdint.h>

/* returns compressed size or 0 if it would not fit in cap */
size_t huff_compress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap);
/* returns decompressed size or -1 on corrupt input */
long   huff_decompress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap);

#endif
