/* ============================================================
 * pattern.c - 通配符模式匹配实现（带回溯）
 *   '*' 任意长度（可 0），'?' 恰好一个字符并产生捕获
 *   文件名场景：ASCII 大小写不敏感（与 Windows 文件名/FindFirstFileW 语义一致）
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wctype.h>
#include "pattern.h"

typedef struct {
    wchar_t* cap;       /* 捕获缓冲（可 NULL） */
    size_t   caplen;    /* 缓冲容量            */
    size_t   capn;      /* 当前匹配路径上捕获的总字符数（可大于缓冲容量） */
} CAPTURE;

static void cap_put(CAPTURE* c, wchar_t ch)
{
    size_t pos;
    if (!c) return;
    pos = c->capn;      /* 先计数：即使缓冲不足，也能知道完整捕获长度 */
    c->capn++;
    if (!c->cap || c->caplen == 0) return;
    /* 预留 1 个 wchar_t 放结尾 NUL；超长部分只计数不写（匹配结论不受影响） */
    if (pos < c->caplen - 1) c->cap[pos] = ch;
}

/* 文件名比较：ASCII 大小写不敏感（非 ASCII 原样比较） */
static BOOL wch_eq(wchar_t a, wchar_t b)
{
    if (a == b) return TRUE;
    if (a > 0x7F || b > 0x7F) return FALSE;
    return towupper((wint_t)a) == towupper((wint_t)b);
}

/* 递归回溯匹配：pi=模式游标 nj=名字游标
 * 注意 '*' 的消耗上界必须是“剩余字符数 wcslen(n)-nj”，
 * 不能是已消耗下标 nj（旧实现 k=nj 会导致 '*' 无法吃字符，且下标可达 2*nj 造成越界读）。 */
static BOOL mrec(const wchar_t* p, size_t pi, const wchar_t* n, size_t nj, CAPTURE* cap)
{
    while (p[pi]) {
        if (p[pi] == L'*') {
            /* 连续 '*' 合并 */
            while (p[pi] == L'*') pi++;
            if (!p[pi]) return TRUE;
            size_t nlen = wcslen(n);
            if (nj > nlen) return FALSE;
            /* 尝试 '*' 消耗 0..剩余 个字符（升序：先少后多） */
            for (size_t c = 0; nj + c <= nlen; c++) {
                size_t saved = cap ? cap->capn : 0;
                if (mrec(p, pi, n, nj + c, cap)) return TRUE;
                if (cap) cap->capn = saved;   /* 回溯撤销失败路径的捕获 */
                /* 注意：'*' 不产生捕获，捕获仅来自 '?' */
            }
            return FALSE;
        } else if (p[pi] == L'?') {
            if (!n[nj]) return FALSE;
            cap_put(cap, n[nj]);
            pi++;
            nj++;
        } else {
            if (!n[nj] || !wch_eq(n[nj], p[pi])) return FALSE;
            pi++;
            nj++;
        }
    }
    return n[nj] == 0;
}

static BOOL match_impl(const wchar_t* pattern, const wchar_t* name,
                       wchar_t* cap1, size_t cap1len, BOOL* truncated)
{
    CAPTURE c;
    BOOL ok;

    if (truncated) *truncated = FALSE;
    if (!pattern || !name) {
        if (cap1 && cap1len) cap1[0] = 0;
        return FALSE;
    }
    c.cap = cap1;
    c.caplen = cap1len;
    c.capn = 0;
    /* 即使 cap1 为 NULL 也传入 CAPTURE 统计捕获总数（供 callers 判断截断） */
    ok = mrec(pattern, 0, name, 0, &c);
    if (cap1 && cap1len) {
        if (ok) {
            size_t write = c.capn;
            if (write > cap1len - 1) {
                write = cap1len - 1;          /* 超长：cap 保留前缀，标记截断 */
                if (truncated) *truncated = TRUE;
            }
            cap1[write] = 0;
        } else {
            /* 匹配失败：清空（与旧实现一致，避免调用方误用半截捕获） */
            cap1[0] = 0;
        }
    }
    return ok;
}

BOOL pat_match_capture(const wchar_t* pattern, const wchar_t* name,
                       wchar_t* cap1, size_t cap1len)
{
    return match_impl(pattern, name, cap1, cap1len, NULL);
}

BOOL pat_match_capture_ex(const wchar_t* pattern, const wchar_t* name,
                          wchar_t* cap1, size_t cap1len, BOOL* truncated)
{
    return match_impl(pattern, name, cap1, cap1len, truncated);
}

BOOL pat_match(const wchar_t* pattern, const wchar_t* name)
{
    return match_impl(pattern, name, NULL, 0, NULL);
}
