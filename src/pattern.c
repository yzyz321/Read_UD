/* ============================================================
 * pattern.c - 通配符模式匹配实现（带回溯）
 * ============================================================ */
#include <string.h>
#include "pattern.h"

typedef struct {
    wchar_t* cap;       /* 捕获缓冲（可 NULL） */
    size_t   caplen;    /* 缓冲容量            */
    size_t   capn;      /* 已捕获长度          */
} CAPTURE;

static void cap_put(CAPTURE* c, wchar_t ch)
{
    if (!c || !c->cap) return;
    if (c->capn + 1 < c->caplen) c->cap[c->capn++] = ch;
}

/* 递归回溯匹配：pi=模式游标 pj=名字游标 */
static BOOL mrec(const wchar_t* p, size_t pi, const wchar_t* n, size_t nj, CAPTURE* cap)
{
    while (p[pi]) {
        if (p[pi] == L'*') {
            /* 连续 '*' 合并 */
            while (p[pi] == L'*') pi++;
            if (!p[pi]) return TRUE;
            /* 尝试 '*' 消耗 0..k 个字符 */
            size_t k = nj;
            size_t saved = cap ? cap->capn : 0;
            for (;;) {
                if (mrec(p, pi, n, nj + k, cap)) return TRUE;
                if (cap) cap->capn = saved;   /* 回溯撤销失败路径的捕获 */
                if (k == 0) break;
                k--;
                /* 注意：'*' 不产生捕获，捕获仅来自 '?' */
            }
            return FALSE;
        } else if (p[pi] == L'?') {
            if (!n[nj]) return FALSE;
            cap_put(cap, n[nj]);
            pi++;
            nj++;
        } else {
            if (n[nj] != p[pi]) return FALSE;
            pi++;
            nj++;
        }
    }
    return n[nj] == 0;
}

static BOOL match_impl(const wchar_t* pattern, const wchar_t* name,
                       wchar_t* cap1, size_t cap1len)
{
    CAPTURE c;
    c.cap = cap1;
    c.caplen = cap1len;
    c.capn = 0;
    BOOL ok = mrec(pattern, 0, name, 0, cap1 ? &c : NULL);
    if (ok && cap1 && cap1len) {
        c.cap[c.capn] = 0;
    } else if (cap1 && cap1len) {
        cap1[0] = 0;
    }
    return ok;
}

BOOL pat_match_capture(const wchar_t* pattern, const wchar_t* name,
                       wchar_t* cap1, size_t cap1len)
{
    return match_impl(pattern, name, cap1, cap1len);
}

BOOL pat_match(const wchar_t* pattern, const wchar_t* name)
{
    return match_impl(pattern, name, NULL, 0);
}
