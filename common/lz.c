/*
 * KLZ - hash-chain LZ77 compressor.  See lz.h for the format.
 *
 * Not reentrant: the match finder state is static so the kernel does not
 * need 200KB of stack.  The kernel never compresses from interrupt context.
 */
#include "lz.h"

#define HASH_BITS 14
#define HASH_SIZE (1u << HASH_BITS)
#define WINDOW    65535u
#define MIN_MATCH 4

/* head[] stores (base + pos); entries below base belong to older calls,
 * so we never have to clear the table between calls. */
static uint32_t head[HASH_SIZE];
static uint16_t prevd[65536];   /* distance to previous pos with same hash */
static uint32_t base = 1;

static inline uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint32_t hash4(const uint8_t *p)
{
    return (rd32(p) * 2654435761u) >> (32 - HASH_BITS);
}

static inline void insert(const uint8_t *src, uint32_t pos)
{
    uint32_t h = hash4(src + pos);
    uint32_t old = head[h];
    uint32_t d = 0;
    if (old >= base) {
        uint32_t dist = pos - (old - base);
        if (dist <= WINDOW)
            d = dist;
    }
    prevd[pos & 0xffff] = (uint16_t)d;
    head[h] = base + pos;
}

/* find the longest match for src[pos..], walking at most depth links */
static uint32_t find(const uint8_t *src, uint32_t pos, uint32_t n,
                     int depth, uint32_t *off)
{
    uint32_t h = hash4(src + pos);
    uint32_t cand = head[h];
    uint32_t best = 0, limit = n - pos;
    if (cand < base)
        return 0;
    uint32_t cpos = cand - base;
    while (depth-- > 0 && cpos < pos) {
        uint32_t dist = pos - cpos;
        if (dist > WINDOW)
            break;
        const uint8_t *a = src + cpos, *b = src + pos;
        if (a[best] == b[best] && rd32(a) == rd32(b)) {
            uint32_t l = 4;
            while (l < limit && a[l] == b[l])
                l++;
            if (l > best) {
                best = l;
                *off = dist;
                if (l == limit)
                    break;
            }
        }
        uint16_t d = prevd[cpos & 0xffff];
        if (d == 0 || d > cpos)
            break;
        cpos -= d;
    }
    return best >= MIN_MATCH ? best : 0;
}

static uint8_t *put_len(uint8_t *op, uint8_t *oend, uint32_t len)
{
    while (len >= 255) {
        if (op >= oend) return 0;
        *op++ = 255;
        len -= 255;
    }
    if (op >= oend) return 0;
    *op++ = (uint8_t)len;
    return op;
}

static uint8_t *emit(uint8_t *op, uint8_t *oend, const uint8_t *lit,
                     uint32_t nlit, uint32_t mlen, uint32_t off)
{
    uint8_t *tok = op++;
    if (op > oend) return 0;
    uint32_t ml = mlen ? mlen - MIN_MATCH : 0;
    *tok = (uint8_t)(((nlit < 15 ? nlit : 15) << 4) | (ml < 15 ? ml : 15));
    if (nlit >= 15 && !(op = put_len(op, oend, nlit - 15)))
        return 0;
    if ((size_t)(oend - op) < nlit) return 0;
    for (uint32_t i = 0; i < nlit; i++)
        op[i] = lit[i];
    op += nlit;
    if (mlen) {
        if (oend - op < 2) return 0;
        *op++ = (uint8_t)off;
        *op++ = (uint8_t)(off >> 8);
        if (ml >= 15 && !(op = put_len(op, oend, ml - 15)))
            return 0;
    }
    return op;
}

size_t lz_compress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap,
                   int level)
{
    uint8_t *op = dst, *oend = dst + cap;
    uint32_t pos = 0, anchor = 0;
    int lazy = level >= 8;

    if (base > 0xF0000000u - n) {   /* table generation wrap */
        for (uint32_t i = 0; i < HASH_SIZE; i++)
            head[i] = 0;
        base = 1;
    }

    if (n >= MIN_MATCH + 1) {
        uint32_t last = (uint32_t)n - MIN_MATCH;
        while (pos <= last) {
            uint32_t off = 0, len = find(src, pos, (uint32_t)n, level, &off);
            if (!len) {
                insert(src, pos);
                pos++;
                continue;
            }
            if (lazy && pos + 1 <= last) {
                /* lazy evaluation: prefer a longer match one byte later */
                uint32_t off2 = 0, len2;
                insert(src, pos);
                len2 = find(src, pos + 1, (uint32_t)n, level, &off2);
                if (len2 > len + 1) {
                    pos++;
                    len = len2;
                    off = off2;
                }
            }
            op = emit(op, oend, src + anchor, pos - anchor, len, off);
            if (!op) return 0;
            uint32_t end = pos + len;
            uint32_t ins_end = end <= last ? end : last + 1;
            /* the fast level skips insertion inside matches */
            if (level > 1 || len < 32) {
                for (uint32_t p = pos; p < ins_end; p++)
                    if (!lazy || p != pos) insert(src, p);
            } else {
                insert(src, pos);
            }
            pos = end;
            anchor = pos;
        }
    }
    op = emit(op, oend, src + anchor, (uint32_t)n - anchor, 0, 0);
    base += (uint32_t)n + WINDOW + 1;
    if (!op) return 0;
    return (size_t)(op - dst);
}

long lz_decompress(const uint8_t *src, size_t n, uint8_t *dst, size_t cap)
{
    const uint8_t *ip = src, *iend = src + n;
    uint8_t *op = dst, *oend = dst + cap;

    while (ip < iend) {
        uint32_t tok = *ip++;
        uint32_t nlit = tok >> 4;
        if (nlit == 15) {
            uint32_t b;
            do {
                if (ip >= iend) return -1;
                b = *ip++;
                nlit += b;
            } while (b == 255);
        }
        if ((size_t)(iend - ip) < nlit || (size_t)(oend - op) < nlit)
            return -1;
        for (uint32_t i = 0; i < nlit; i++)
            op[i] = ip[i];
        ip += nlit;
        op += nlit;
        if (ip >= iend)
            break;
        if (iend - ip < 2) return -1;
        uint32_t off = ip[0] | ((uint32_t)ip[1] << 8);
        ip += 2;
        uint32_t mlen = tok & 15;
        if (mlen == 15) {
            uint32_t b;
            do {
                if (ip >= iend) return -1;
                b = *ip++;
                mlen += b;
            } while (b == 255);
        }
        mlen += MIN_MATCH;
        if (off == 0 || off > (size_t)(op - dst) || (size_t)(oend - op) < mlen)
            return -1;
        const uint8_t *m = op - off;
        for (uint32_t i = 0; i < mlen; i++)   /* overlapping copy */
            op[i] = m[i];
        op += mlen;
    }
    return (long)(op - dst);
}

/* ---- method selection: raw / LZ / LZ + Huffman ---- */
#include "huff.h"

size_t lz_pack(const uint8_t *src, size_t n, uint8_t *dst, size_t cap,
               uint8_t *tmp, int level, int *method)
{
    size_t lz = lz_compress(src, n, tmp, LZ_BOUND(n), level);
    size_t best = n;
    *method = PACK_RAW;
    if (lz && lz < best && lz <= cap) {
        best = lz;
        *method = PACK_LZ;
    }
    if (lz) {
        size_t h = huff_compress(tmp, lz, dst, cap);
        if (h && h < best) {
            *method = PACK_LZH;
            return h;
        }
    }
    if (*method == PACK_LZ) {
        for (size_t i = 0; i < lz; i++)
            dst[i] = tmp[i];
        return lz;
    }
    if (n > cap)
        return 0;
    for (size_t i = 0; i < n; i++)
        dst[i] = src[i];
    return n;
}

long lz_unpack(const uint8_t *src, size_t n, uint8_t *dst, size_t cap,
               uint8_t *tmp, size_t tmpcap, int method)
{
    if (method == PACK_RAW) {
        if (n > cap)
            return -1;
        for (size_t i = 0; i < n; i++)
            dst[i] = src[i];
        return (long)n;
    }
    if (method == PACK_LZ)
        return lz_decompress(src, n, dst, cap);
    long l = huff_decompress(src, n, tmp, tmpcap);
    if (l < 0)
        return -1;
    return lz_decompress(tmp, (size_t)l, dst, cap);
}
