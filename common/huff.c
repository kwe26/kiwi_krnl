/*
 * Canonical Huffman coding of bytes - the entropy stage of KLZ "heavy"
 * mode (LZ77 first, then Huffman over the LZ stream).
 *
 * Format:  u16 nsyms-1 present  ... see huff.h
 */
#include "huff.h"

#define MAXLEN 15

struct node { uint32_t freq; int16_t left, right; };

/* compute code lengths (<= MAXLEN) for freq[256] */
static void build_lengths(const uint32_t *freq_in, uint8_t *len)
{
    uint32_t freq[256];
    for (int i = 0; i < 256; i++)
        freq[i] = freq_in[i];
    for (;;) {
        struct node nodes[512];
        int heap[256], hn = 0, nn = 0;
        for (int i = 0; i < 256; i++) {
            len[i] = 0;
            if (freq[i]) {
                nodes[nn] = (struct node){ freq[i], -1, (int16_t)i };
                heap[hn++] = nn++;
            }
        }
        if (hn == 0)
            return;
        if (hn == 1) {
            len[nodes[0].right] = 1;
            return;
        }
        /* O(n^2) selection is fine for 256 symbols */
        while (hn > 1) {
            int a = 0, b = 1;
            if (nodes[heap[b]].freq < nodes[heap[a]].freq) { a = 1; b = 0; }
            for (int i = 2; i < hn; i++) {
                if (nodes[heap[i]].freq < nodes[heap[a]].freq) { b = a; a = i; }
                else if (nodes[heap[i]].freq < nodes[heap[b]].freq) b = i;
            }
            nodes[nn] = (struct node){ nodes[heap[a]].freq + nodes[heap[b]].freq,
                                       (int16_t)heap[a], (int16_t)heap[b] };
            int hi = a > b ? a : b, lo = a > b ? b : a;
            heap[hi] = heap[--hn];
            heap[lo] = nn++;
        }
        /* depth-first walk to assign lengths */
        int stack[512], depth[512], sp = 0, too_long = 0;
        stack[sp] = heap[0];
        depth[sp++] = 0;
        while (sp) {
            sp--;
            int n = stack[sp], d = depth[sp];
            if (nodes[n].left < 0) {
                len[nodes[n].right] = (uint8_t)d;
                if (d > MAXLEN) too_long = 1;
            } else {
                stack[sp] = nodes[n].left;  depth[sp++] = d + 1;
                stack[sp] = nodes[n].right; depth[sp++] = d + 1;
            }
        }
        if (!too_long)
            return;
        for (int i = 0; i < 256; i++)       /* flatten and retry */
            if (freq[i])
                freq[i] = (freq[i] >> 1) | 1;
    }
}

/* canonical codes from lengths */
static void assign_codes(const uint8_t *len, uint16_t *code)
{
    uint16_t count[MAXLEN + 2] = { 0 }, next[MAXLEN + 2];
    for (int i = 0; i < 256; i++)
        count[len[i]]++;
    count[0] = 0;
    uint16_t c = 0;
    for (int l = 1; l <= MAXLEN; l++) {
        c = (uint16_t)((c + count[l - 1]) << 1);
        next[l] = c;
    }
    for (int i = 0; i < 256; i++)
        if (len[i])
            code[i] = next[len[i]]++;
}

size_t huff_compress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap)
{
    if (n == 0 || n > 0xFFFFFFu || cap < 4 + 128)
        return 0;
    uint32_t freq[256] = { 0 };
    for (size_t i = 0; i < n; i++)
        freq[src[i]]++;
    uint8_t len[256];
    uint16_t code[256];
    build_lengths(freq, len);
    assign_codes(len, code);

    uint8_t *op = dst;
    *op++ = (uint8_t)n;
    *op++ = (uint8_t)(n >> 8);
    *op++ = (uint8_t)(n >> 16);
    for (int i = 0; i < 256; i += 2)
        *op++ = (uint8_t)(len[i] | (len[i + 1] << 4));
    uint8_t *oend = dst + cap;
    uint32_t bits = 0;
    int nbits = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t s = src[i];
        bits = (bits << len[s]) | code[s];
        nbits += len[s];
        while (nbits >= 8) {
            if (op >= oend)
                return 0;
            nbits -= 8;
            *op++ = (uint8_t)(bits >> nbits);
        }
    }
    if (nbits) {
        if (op >= oend)
            return 0;
        *op++ = (uint8_t)(bits << (8 - nbits));
    }
    return (size_t)(op - dst);
}

long huff_decompress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap)
{
    if (n < 3 + 128)
        return -1;
    size_t out = src[0] | ((size_t)src[1] << 8) | ((size_t)src[2] << 16);
    if (out > cap)
        return -1;
    uint8_t len[256];
    for (int i = 0; i < 128; i++) {
        len[2 * i] = src[3 + i] & 15;
        len[2 * i + 1] = src[3 + i] >> 4;
    }
    /* canonical decoding tables */
    uint16_t count[MAXLEN + 1] = { 0 }, offs[MAXLEN + 2];
    uint8_t syms[256];
    for (int i = 0; i < 256; i++)
        count[len[i]]++;
    count[0] = 0;
    offs[1] = 0;
    for (int l = 1; l <= MAXLEN; l++)
        offs[l + 1] = (uint16_t)(offs[l] + count[l]);
    {
        uint16_t pos[MAXLEN + 2];
        for (int l = 1; l <= MAXLEN + 1; l++)
            pos[l] = offs[l];
        for (int i = 0; i < 256; i++)
            if (len[i])
                syms[pos[len[i]]++] = (uint8_t)i;
    }
    /* 8-bit fast lookup: sym | (len << 8), 0 = needs the slow path */
    uint16_t fast[256];
    for (int i = 0; i < 256; i++)
        fast[i] = 0;
    {
        int code = 0, k = 0;
        for (int l = 1; l <= 8; l++) {
            for (int j = 0; j < count[l]; j++, k++, code++) {
                int span = 1 << (8 - l);
                for (int x = 0; x < span; x++)
                    fast[(code << (8 - l)) | x] = (uint16_t)(syms[k] | (l << 8));
            }
            code <<= 1;
        }
    }
    const uint8_t *ip = src + 3 + 128, *iend = src + n;
    uint32_t bits = 0;
    int nbits = 0;
    for (size_t o = 0; o < out; o++) {
        while (nbits <= 24) {
            bits = (bits << 8) | (ip < iend ? *ip : 0);
            if (ip < iend) ip++;
            nbits += 8;
        }
        uint16_t f = fast[(bits >> (nbits - 8)) & 0xFF];
        if (f) {
            dst[o] = (uint8_t)f;
            nbits -= f >> 8;
            continue;
        }
        int code = 0, first = 0, index = 0, l;
        for (l = 1; l <= MAXLEN; l++) {
            code |= (int)((bits >> (nbits - l)) & 1);
            int c = count[l];
            if (code - first < c) {
                dst[o] = syms[index + code - first];
                break;
            }
            index += c;
            first = (first + c) << 1;
            code <<= 1;
        }
        if (l > MAXLEN)
            return -1;
        nbits -= l;
    }
    return (long)out;
}
