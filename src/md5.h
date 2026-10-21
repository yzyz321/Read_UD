/* ============================================================
 * md5.h - RFC1321 MD5 摘要（用于 HASH=md5 校验）
 * ============================================================ */
#ifndef UDK_MD5_H
#define UDK_MD5_H

#include <windows.h>

typedef struct {
    unsigned int state[4];
    unsigned int count[2];
    unsigned char buffer[64];
} md5_context;

void md5_init(md5_context* ctx);
void md5_update(md5_context* ctx, const unsigned char* data, size_t len);
void md5_final(md5_context* ctx, unsigned char digest[16]);

#endif
