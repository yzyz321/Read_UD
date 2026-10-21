/* ============================================================
 * md5.c - RFC1321 MD5 摘要实现
 * ============================================================ */
#include <string.h>
#include "md5.h"

#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z)))
#define ROTL(x, c) (((x) << (c)) | ((x) >> (32 - (c))))
#define STEP(f, a, b, c, d, x, t, s) \
    (a) += f((b), (c), (d)) + (x) + (t); (a) = ROTL((a), (s)); (a) += (b);

static void md5_transform(unsigned int state[4], const unsigned char block[64])
{
    unsigned int a = state[0], b = state[1], c = state[2], d = state[3];
    unsigned int x[16];
    for (int i = 0; i < 16; i++)
        x[i] = (unsigned int)block[i * 4] | ((unsigned int)block[i * 4 + 1] << 8) |
               ((unsigned int)block[i * 4 + 2] << 16) | ((unsigned int)block[i * 4 + 3] << 24);

    STEP(F, a, b, c, d, x[0], 0xd76aa478, 7)
    STEP(F, d, a, b, c, x[1], 0xe8c7b756, 12)
    STEP(F, c, d, a, b, x[2], 0x242070db, 17)
    STEP(F, b, c, d, a, x[3], 0xc1bdceee, 22)
    STEP(F, a, b, c, d, x[4], 0xf57c0faf, 7)
    STEP(F, d, a, b, c, x[5], 0x4787c62a, 12)
    STEP(F, c, d, a, b, x[6], 0xa8304613, 17)
    STEP(F, b, c, d, a, x[7], 0xfd469501, 22)
    STEP(F, a, b, c, d, x[8], 0x698098d8, 7)
    STEP(F, d, a, b, c, x[9], 0x8b44f7af, 12)
    STEP(F, c, d, a, b, x[10], 0xffff5bb1, 17)
    STEP(F, b, c, d, a, x[11], 0x895cd7be, 22)
    STEP(F, a, b, c, d, x[12], 0x6b901122, 7)
    STEP(F, d, a, b, c, x[13], 0xfd987193, 12)
    STEP(F, c, d, a, b, x[14], 0xa679438e, 17)
    STEP(F, b, c, d, a, x[15], 0x49b40821, 22)

    STEP(G, a, b, c, d, x[1], 0xf61e2562, 5)
    STEP(G, d, a, b, c, x[6], 0xc040b340, 9)
    STEP(G, c, d, a, b, x[11], 0x265e5a51, 14)
    STEP(G, b, c, d, a, x[0], 0xe9b6c7aa, 20)
    STEP(G, a, b, c, d, x[5], 0xd62f105d, 5)
    STEP(G, d, a, b, c, x[10], 0x02441453, 9)
    STEP(G, c, d, a, b, x[15], 0xd8a1e681, 14)
    STEP(G, b, c, d, a, x[4], 0xe7d3fbc8, 20)
    STEP(G, a, b, c, d, x[9], 0x21e1cde6, 5)
    STEP(G, d, a, b, c, x[14], 0xc33707d6, 9)
    STEP(G, c, d, a, b, x[3], 0xf4d50d87, 14)
    STEP(G, b, c, d, a, x[8], 0x455a14ed, 20)
    STEP(G, a, b, c, d, x[13], 0xa9e3e905, 5)
    STEP(G, d, a, b, c, x[2], 0xfcefa3f8, 9)
    STEP(G, c, d, a, b, x[7], 0x676f02d9, 14)
    STEP(G, b, c, d, a, x[12], 0x8d2a4c8a, 20)

    STEP(H, a, b, c, d, x[5], 0xfffa3942, 4)
    STEP(H, d, a, b, c, x[8], 0x8771f681, 11)
    STEP(H, c, d, a, b, x[11], 0x6d9d6122, 16)
    STEP(H, b, c, d, a, x[14], 0xfde5380c, 23)
    STEP(H, a, b, c, d, x[1], 0xa4beea44, 4)
    STEP(H, d, a, b, c, x[4], 0x4bdecfa9, 11)
    STEP(H, c, d, a, b, x[7], 0xf6bb4b60, 16)
    STEP(H, b, c, d, a, x[10], 0xbebfbc70, 23)
    STEP(H, a, b, c, d, x[13], 0x289b7ec6, 4)
    STEP(H, d, a, b, c, x[0], 0xeaa127fa, 11)
    STEP(H, c, d, a, b, x[3], 0xd4ef3085, 16)
    STEP(H, b, c, d, a, x[6], 0x04881d05, 23)
    STEP(H, a, b, c, d, x[9], 0xd9d4d039, 4)
    STEP(H, d, a, b, c, x[12], 0xe6db99e5, 11)
    STEP(H, c, d, a, b, x[15], 0x1fa27cf8, 16)
    STEP(H, b, c, d, a, x[2], 0xc4ac5665, 23)

    STEP(I, a, b, c, d, x[0], 0xf4292244, 6)
    STEP(I, d, a, b, c, x[7], 0x432aff97, 10)
    STEP(I, c, d, a, b, x[14], 0xab9423a7, 15)
    STEP(I, b, c, d, a, x[5], 0xfc93a039, 21)
    STEP(I, a, b, c, d, x[12], 0x655b59c3, 6)
    STEP(I, d, a, b, c, x[3], 0x8f0ccc92, 10)
    STEP(I, c, d, a, b, x[10], 0xffeff47d, 15)
    STEP(I, b, c, d, a, x[1], 0x85845dd1, 21)
    STEP(I, a, b, c, d, x[8], 0x6fa87e4f, 6)
    STEP(I, d, a, b, c, x[15], 0xfe2ce6e0, 10)
    STEP(I, c, d, a, b, x[6], 0xa3014314, 15)
    STEP(I, b, c, d, a, x[13], 0x4e0811a1, 21)
    STEP(I, a, b, c, d, x[4], 0xf7537e82, 6)
    STEP(I, d, a, b, c, x[11], 0xbd3af235, 10)
    STEP(I, c, d, a, b, x[2], 0x2ad7d2bb, 15)
    STEP(I, b, c, d, a, x[9], 0xeb86d391, 21)

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

void md5_init(md5_context* ctx)
{
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

void md5_update(md5_context* ctx, const unsigned char* data, size_t len)
{
    unsigned int i, idx, part;
    idx = (unsigned int)((ctx->count[0] >> 3) & 0x3F);
    ctx->count[0] += (unsigned int)(len << 3);
    if (ctx->count[0] < (unsigned int)(len << 3)) ctx->count[1]++;
    ctx->count[1] += (unsigned int)(len >> 29);

    part = 64 - idx;
    if (len >= part) {
        memcpy(&ctx->buffer[idx], data, part);
        md5_transform(ctx->state, ctx->buffer);
        for (i = part; i + 63 < len; i += 64)
            md5_transform(ctx->state, &data[i]);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[idx], &data[i], len - i);
}

static const unsigned char pad[64] = { 0x80 };

void md5_final(md5_context* ctx, unsigned char digest[16])
{
    unsigned char bits[8];
    unsigned int i, idx, padlen;
    for (i = 0; i < 2; i++) {
        bits[i * 4 + 0] = (unsigned char)(ctx->count[i] & 0xFF);
        bits[i * 4 + 1] = (unsigned char)((ctx->count[i] >> 8) & 0xFF);
        bits[i * 4 + 2] = (unsigned char)((ctx->count[i] >> 16) & 0xFF);
        bits[i * 4 + 3] = (unsigned char)((ctx->count[i] >> 24) & 0xFF);
    }
    idx = (unsigned int)((ctx->count[0] >> 3) & 0x3F);
    padlen = (idx < 56) ? (56 - idx) : (120 - idx);
    md5_update(ctx, pad, padlen);
    md5_update(ctx, bits, 8);
    for (i = 0; i < 4; i++) {
        digest[i * 4 + 0] = (unsigned char)(ctx->state[i] & 0xFF);
        digest[i * 4 + 1] = (unsigned char)((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 2] = (unsigned char)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 3] = (unsigned char)((ctx->state[i] >> 24) & 0xFF);
    }
}
