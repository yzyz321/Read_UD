/* test_pattern.c - 通配符匹配单元测试（* ? 回溯/大小写/捕获/截断/NULL）\n * 覆盖审计修复项 5b（回溯上界/大小写）与 P2-16 捕获截断显式上报。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "pattern.h"

static int pass = 0, fail = 0;

static void check(const wchar_t* pat, const wchar_t* name, BOOL expect, const wchar_t* exp_cap)
{
    wchar_t cap[256] = L"\x01";
    BOOL r = pat_match_capture(pat, name, cap, 256);
    BOOL ok = (r == expect);
    if (ok && exp_cap) ok = (wcscmp(cap, exp_cap) == 0);
    if (ok) pass++; else fail++;
    if (!ok)
        wprintf(L"FAIL pat=\"%ls\" name=\"%ls\" got=%ls expect=%ls cap=\"%ls\" expcap=\"%ls\"\n",
                pat, name, r ? L"TRUE" : L"FALSE", expect ? L"TRUE" : L"FALSE", cap, exp_cap ? exp_cap : L"-");
}

int main(void)
{
    /* 审计报告中的 11 个失败用例（修复前全 FAIL） */
    check(L"*.txt",          L"a.txt",          TRUE,  NULL);
    check(L"*a",             L"xa",             TRUE,  NULL);
    check(L"*abc",           L"xxabc",          TRUE,  NULL);
    check(L"*.bin",          L"f.bin",          TRUE,  NULL);
    check(L"a*b",            L"axxb",           TRUE,  NULL);
    check(L"a*b?c",          L"aXXbYc",         TRUE,  L"Y");
    check(L"*?c",            L"abc",            TRUE,  L"b");
    check(L"*?",             L"abc",            TRUE,  L"c");
    check(L"*a*b*c",         L"xxaYYbZZc",      TRUE,  NULL);
    check(L"*",              L"anything",       TRUE,  NULL);
    check(L"**",             L"",               TRUE,  NULL);
    check(L"*abc",           L"xabc",           TRUE,  NULL);

    /* 原有正确用例不能被破坏 */
    check(L"8685????",       L"86851234",       TRUE,  L"1234");
    check(L"8685????.txt",   L"86851234.txt",   TRUE,  L"1234");
    check(L"GTB907785????",  L"GTB907785AB12",  TRUE,  L"AB12");
    check(L"a*b*c",          L"aXbYc",          TRUE,  NULL);
    check(L"abc",            L"abcd",           FALSE, L"");
    check(L"abc",            L"abc",            TRUE,  L"");
    check(L"????",           L"abc",            FALSE, L"");
    check(L"",               L"",               TRUE,  L"");
    check(L"",               L"x",              FALSE, L"");
    check(L"a?c",            L"abc",            TRUE,  L"b");
    check(L"a*c",            L"ac",             TRUE,  NULL);
    check(L"*.*",            L"readme.txt",     TRUE,  NULL);
    check(L"SYSTEM\\*.txt",  L"paw6k.txt",      FALSE, L"");   /* 带路径的模式不匹配纯文件名（由调用方拆分目录，不应崩） */
    check(L"?",              L"ab",             FALSE, L"");
    check(L"a*b*c*d*e",      L"aXbYcZdWe",      TRUE,  NULL);
    check(L"*A*",            L"xAy",            TRUE,  NULL);

    /* 大小写不敏感（文件名语义） */
    check(L"GTB907785????",  L"gtb907785ab12",  TRUE,  L"ab12");
    check(L"*.TXT",          L"readme.txt",     TRUE,  NULL);
    check(L"ABC",            L"abc",            TRUE,  NULL);
    check(L"*K2411*",        L"x-k2411-y",      TRUE,  NULL);

    /* 连续星号合并 + 复杂组合 */
    check(L"a**b",           L"aXb",            TRUE,  NULL);
    check(L"*a**b*",         L"QaXbR",          TRUE,  NULL);
    check(L"?*?",            L"abc",            TRUE,  L"ac");   /* 捕获仅 '?' 消耗的字符 */
    check(L"*.*",            L"a.b.c",          TRUE,  NULL);

    /* NULL / 空缓冲 */
    {
        wchar_t cap[8];
        BOOL r1 = pat_match(NULL, L"x");
        BOOL r2 = pat_match(L"x", NULL);
        BOOL r3 = pat_match_capture(L"x", L"x", NULL, 0);
        BOOL r4 = pat_match_capture(L"??", L"ab", cap, 2);   /* 容量 2：只能放 1 字符 + NUL */
        int ok = (r1 == FALSE && r2 == FALSE && r3 == TRUE && r4 == TRUE && wcscmp(cap, L"a") == 0);
        if (ok) pass++; else fail++;
        if (!ok) wprintf(L"FAIL NULL/cap cases: %d %d %d %d cap=\"%ls\"\n", r1, r2, r3, r4, cap);
    }

    /* 捕获截断显式上报（P2-16 残余） */
    {
        wchar_t cap2[8];
        BOOL tr = FALSE;
        const wchar_t* p20 = L"GTB907785????????????????????";
        const wchar_t* n20 = L"GTB907785ABCDEFGHIJKLMNOPQRST";
        BOOL r = pat_match_capture_ex(p20, n20, cap2, 8, &tr);
        int okk = (r == TRUE && tr == TRUE && wcslen(cap2) == 7 &&
                   wcscmp(cap2, L"ABCDEFG") == 0);
        if (okk) pass++;
        else { fail++; wprintf(L"FAIL ex truncated r=%d tr=%d cap=\"%ls\"\n", r, tr, cap2); }

        tr = TRUE;
        {
            wchar_t big[32];
            r = pat_match_capture_ex(p20, n20, big, 32, &tr);
            okk = (r == TRUE && tr == FALSE && wcscmp(big, L"ABCDEFGHIJKLMNOPQRST") == 0);
            if (okk) pass++; else { fail++; wprintf(L"FAIL ex full r=%d tr=%d\n", r, tr); }
        }
        tr = TRUE;
        okk = (pat_match_capture_ex(p20, n20, NULL, 0, &tr) == TRUE && tr == FALSE);
        if (okk) pass++; else fail++;
        cap2[0] = 0; tr = TRUE;
        okk = (pat_match_capture_ex(NULL, n20, cap2, 8, &tr) == FALSE && tr == FALSE && cap2[0] == 0);
        if (okk) pass++; else fail++;
        {
            wchar_t old[8];
            okk = (pat_match_capture(p20, n20, old, 8) == TRUE && wcscmp(old, L"ABCDEFG") == 0);
            if (okk) pass++; else fail++;
        }
    }

    wprintf(L"\n[pattern] PASS=%d FAIL=%d\n", pass, fail);
    return fail ? 1 : 0;
}


