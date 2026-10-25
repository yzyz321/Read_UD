/* test_encoding.c - 统一编码探测/解析健壮性/config API/默认配置一致性单元测试
 * 来源：第 4 批验收用例；配置读取使用 cwd=repo 根下的相对路径，
 * 所有写操作限于 %TEMP% 自建目录并在结束时清理。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "util.h"

static int pass = 0, fail = 0;
static wchar_t TDIR[MAX_PATH];


static void ok(BOOL cond, const wchar_t* what)
{
    if (cond) pass++;
    else { fail++; wprintf(L"FAIL: %ls\n", what); }
}

static BOOL wrbytes(const wchar_t* path, const void* data, DWORD len)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w = 0;
    BOOL ok = WriteFile(h, data, len, &w, NULL) && w == len;
    CloseHandle(h); return ok;
}
static BOOL wrstr(const wchar_t* path, const char* s)
{
    return wrbytes(path, s, (DWORD)strlen(s));
}
static char* rdall(const wchar_t* path, DWORD* len)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz; if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return NULL; }
    char* b = (char*)malloc((size_t)sz.QuadPart + 1);
    DWORD rd = 0;
    if (!b || !ReadFile(h, b, (DWORD)sz.QuadPart, &rd, NULL)) { CloseHandle(h); free(b); return NULL; }
    CloseHandle(h);
    ((char*)b)[rd] = 0;
    if (len) *len = rd;
    return b;
}
static BOOL load_utf8(const char* text, CONFIG* cfg)
{
    wchar_t p[MAX_PATH];
    lstrcpynW(p, TDIR, MAX_PATH);
    lstrcatW(p, L"\\t.ini");
    if (!wrstr(p, text)) return FALSE;
    return cfg_load(p, cfg);
}
/* 从宽串生成指定代码页字节 */
static int wide_to_cp(const wchar_t* w, UINT cp, BYTE* out, int cap)
{
    int n = WideCharToMultiByte(cp, 0, w, -1, (char*)out, cap, NULL, NULL);
    return n > 0 ? n - 1 : -1;
}
static int wide_to_utf16be(const wchar_t* w, BYTE* out, int cap)
{
    int n = (int)wcslen(w);
    if (n * 2 > cap) return -1;
    for (int i = 0; i < n; i++) {
        out[i * 2] = (BYTE)(w[i] >> 8);
        out[i * 2 + 1] = (BYTE)(w[i] & 0xFF);
    }
    return n * 2;
}

/* 解析 default_ini.h 的 C 字符串 → UTF-8 字节 */
static char* parse_default_ini(DWORD* outlen)
{
    wchar_t path[MAX_PATH];
    lstrcpynW(path, L".", MAX_PATH);
    lstrcatW(path, L"\\src\\default_ini.h");
    DWORD n = 0;
    char* src = rdall(path, &n);
    if (!src) return NULL;
    char* p = strstr(src, "DEFAULT_INI_UTF8");
    if (!p) { free(src); return NULL; }
    p = strchr(p, '"');
    if (!p) { free(src); return NULL; }
    char* out = (char*)malloc(n + 1);
    if (!out) { free(src); return NULL; }
    size_t o = 0;
    while (*p) {
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\') {
                    p++;
                    switch (*p) {
                    case 'n': out[o++] = '\n'; break;
                    case 'r': out[o++] = '\r'; break;
                    case 't': out[o++] = '\t'; break;
                    case '\\': out[o++] = '\\'; break;
                    case '"': out[o++] = '"'; break;
                    default: out[o++] = *p; break;
                    }
                } else out[o++] = *p;
                p++;
            }
            if (*p) p++;
            continue;
        }
        if (*p == ';') break;   /* 定义结束 */
        p++;
    }
    out[o] = 0;
    free(src);
    if (outlen) *outlen = (DWORD)o;
    return out;
}

static BOOL normalize_file(const wchar_t* path, char** out, DWORD* outlen, BOOL strip_bom)
{
    DWORD n = 0;
    char* b = rdall(path, &n);
    if (!b) return FALSE;
    DWORD start = 0;
    if (strip_bom && n >= 3 && (BYTE)b[0] == 0xEF && (BYTE)b[1] == 0xBB && (BYTE)b[2] == 0xBF) start = 3;
    char* o = (char*)malloc(n + 1);
    size_t k = 0;
    for (DWORD i = start; i < n; i++) {
        if (b[i] == '\r') {
            if (i + 1 < n && b[i + 1] == '\n') { i++; }
            o[k++] = '\n';
        } else o[k++] = b[i];
    }
    o[k] = 0;
    free(b);
    *out = o;
    if (outlen) *outlen = (DWORD)k;
    return TRUE;
}

/* 在 %TEMP% 下建立本进程唯一测试目录（结束清理） */
static void init_temp_dir(void)
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) tmp[0] = 0;
    _snwprintf(TDIR, MAX_PATH, L"%sudk_tests_encoding_%lu\\", tmp,
               (unsigned long)GetCurrentProcessId());
    TDIR[MAX_PATH - 1] = 0;
    CreateDirectoryW(TDIR, NULL);
}

static void cleanup_dir(void)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    lstrcpynW(pat, TDIR, MAX_PATH);
    lstrcatW(pat, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t f[MAX_PATH];
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            lstrcpynW(f, TDIR, MAX_PATH);
            lstrcatW(f, fd.cFileName);
            SetFileAttributesW(f, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(f);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(TDIR);
}

int main(void)
{
    init_temp_dir();

    /* ================= 1. text_decode 编码矩阵 ================= */
    {
        TEXTENC enc; wchar_t* out = NULL; wchar_t err[256];
        /* 纯 UTF-8 */
        const char* u8 = "KEY=\xE4\xB8\xAD\xE6\x96\x87\n";
        ok(text_decode(u8, (DWORD)strlen(u8), &out, &enc, err, 256) == TRUE, L"utf8 plain TRUE");
        ok(enc == TEXT_ENC_UTF8, L"utf8 plain enc");
        ok(wcscmp(out, L"KEY=\u4E2D\u6587\n") == 0, L"utf8 plain text");
        free(out);
        /* UTF-8 + BOM */
        BYTE bom8[64]; bom8[0] = 0xEF; bom8[1] = 0xBB; bom8[2] = 0xBF;
        DWORD n8 = (DWORD)strlen(u8);
        memcpy(bom8 + 3, u8, n8);
        ok(text_decode(bom8, n8 + 3, &out, &enc, err, 256) == TRUE, L"utf8 bom TRUE");
        ok(enc == TEXT_ENC_UTF8_BOM, L"utf8 bom enc");
        free(out);
        /* UTF-16LE + BOM */
        const wchar_t* wtxt = L"KEY=Value\r\n";
        BYTE b16[128]; b16[0] = 0xFF; b16[1] = 0xFE;
        int nb = (int)wcslen(wtxt) * 2;
        memcpy(b16 + 2, wtxt, nb);
        ok(text_decode(b16, nb + 2, &out, &enc, err, 256) == TRUE, L"utf16le bom TRUE");
        ok(enc == TEXT_ENC_UTF16LE && wcscmp(out, wtxt) == 0, L"utf16le bom text");
        free(out);
        /* UTF-16BE + BOM */
        b16[0] = 0xFE; b16[1] = 0xFF;
        nb = wide_to_utf16be(wtxt, b16 + 2, 120);
        ok(nb > 0 && text_decode(b16, nb + 2, &out, &enc, err, 256) == TRUE, L"utf16be bom TRUE");
        ok(enc == TEXT_ENC_UTF16BE && wcscmp(out, wtxt) == 0, L"utf16be bom text");
        free(out);
        /* 无 BOM UTF-16LE / BE（ASCII 主导） */
        memcpy(b16, wtxt, (int)wcslen(wtxt) * 2);
        ok(text_decode(b16, (int)wcslen(wtxt) * 2, &out, &enc, err, 256) == TRUE, L"utf16le nobom TRUE");
        ok(enc == TEXT_ENC_UTF16LE && wcscmp(out, wtxt) == 0, L"utf16le nobom text");
        free(out);
        nb = wide_to_utf16be(wtxt, b16, 120);
        ok(nb > 0 && text_decode(b16, nb, &out, &enc, err, 256) == TRUE, L"utf16be nobom TRUE");
        ok(enc == TEXT_ENC_UTF16BE && wcscmp(out, wtxt) == 0, L"utf16be nobom text");
        free(out);
        /* GBK 中文 → ANSI */
        BYTE gbk[64];
        int ng = wide_to_cp(L"KEY=\u4E2D\u6587\r\n", 936, gbk, 64);
        ok(ng > 0 && text_decode(gbk, ng, &out, &enc, err, 256) == TRUE, L"gbk TRUE");
        ok(enc == TEXT_ENC_ANSI && wcscmp(out, L"KEY=\u4E2D\u6587\r\n") == 0, L"gbk text");
        free(out);
        /* 损坏 UTF-8：合法多字节 + 0x80 */
        BYTE bad[64]; const char* pre = "ABC"; DWORD pb = 3;
        memcpy(bad, pre, pb); bad[pb++] = 0xE4; bad[pb++] = 0xB8; bad[pb++] = 0xAD;
        bad[pb++] = 0x80; bad[pb++] = 'D';
        err[0] = 0;
        ok(text_decode(bad, pb, &out, &enc, err, 256) == FALSE, L"corrupt utf8 FALSE");
        ok(err[0] && (wcsstr(err, L"\u975E\u6CD5") || wcsstr(err, L"UTF-8")), L"corrupt utf8 reason");
        ok(out == NULL, L"corrupt utf8 out NULL");
        /* 二进制 */
        BYTE bin[8] = { 0x00, 0x11, 0x22, 0x00, 0x33, 0x00, 0x44, 0x55 };
        ok(text_decode(bin, 8, &out, &enc, err, 256) == FALSE, L"binary FALSE");
        /* 空输入 */
        ok(text_decode("", 0, &out, &enc, err, 256) == TRUE && out && out[0] == 0, L"empty TRUE empty");
        free(out);
        /* UTF-16 奇数长度 */
        BYTE odd[8] = { 0xFF, 0xFE, 'A', 0x00, 'B' };
        ok(text_decode(odd, 5, &out, &enc, err, 256) == FALSE, L"utf16 odd FALSE");
        /* 内嵌 NUL（UTF-16） */
        BYTE nul16[10] = { 0xFF, 0xFE, 'A', 0x00, 0x00, 0x00, 'B', 0x00 };
        ok(text_decode(nul16, 8, &out, &enc, err, 256) == FALSE, L"utf16 embedded NUL FALSE");
        /* 小 err 缓冲不越界 */
        wchar_t small[4] = { 0 };
        text_decode(bad, pb, &out, &enc, small, 4);
        ok(small[3] == 0, L"small err buffer NUL");
    }

    /* ================= 2. read_file_text ================= */
    {
        wchar_t p[MAX_PATH]; wchar_t* txt = NULL;
        lstrcpynW(p, TDIR, MAX_PATH); lstrcatW(p, L"\\empty.txt");
        wrbytes(p, "", 0);
        SetLastError(0);
        ok(read_file_text(p, &txt) == TRUE && txt && txt[0] == 0, L"read empty TRUE empty");
        free(txt);

        lstrcpynW(p, TDIR, MAX_PATH); lstrcatW(p, L"\\normal.txt");
        wrstr(p, "abc");
        ok(read_file_text(p, &txt) == TRUE && wcscmp(txt, L"abc") == 0, L"read normal");
        free(txt);

        lstrcpynW(p, TDIR, MAX_PATH); lstrcatW(p, L"\\odd16.txt");
        BYTE odd[8] = { 0xFF, 0xFE, 'A', 0x00, 'B' };
        wrbytes(p, odd, 5);
        SetLastError(0);
        ok(read_file_text(p, &txt) == FALSE, L"read odd utf16 FALSE");
        ok(GetLastError() == ERROR_INVALID_DATA, L"read odd utf16 err INVALID_DATA");

        lstrcpynW(p, TDIR, MAX_PATH); lstrcatW(p, L"\\bad8.txt");
        BYTE bad[32]; const char* s = "ABC"; memcpy(bad, s, 3);
        bad[3] = 0xE4; bad[4] = 0xB8; bad[5] = 0xAD; bad[6] = 0x80;
        wrbytes(p, bad, 7);
        SetLastError(0);
        ok(read_file_text(p, &txt) == FALSE, L"read corrupt utf8 FALSE");
        ok(GetLastError() == ERROR_INVALID_DATA, L"read corrupt utf8 err INVALID_DATA");

        lstrcpynW(p, TDIR, MAX_PATH); lstrcatW(p, L"\\big.txt");
        HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER li; li.QuadPart = 65LL * 1024 * 1024;
            SetFilePointerEx(h, li, NULL, FILE_BEGIN);
            SetEndOfFile(h);
            CloseHandle(h);
            SetLastError(0);
            ok(read_file_text(p, &txt) == FALSE, L"read 65MB FALSE");
            ok(GetLastError() == ERROR_FILE_TOO_LARGE, L"read 65MB err FILE_TOO_LARGE");
            DeleteFileW(p);
        } else {
            ok(FALSE, L"create 65MB file");
        }
    }

    /* ================= 3. cfg_load 解析健壮性 ================= */
    {
        static CONFIG c;
        ok(load_utf8(
            "[TOOLSINFO]\nTITLE=T\nVERSION=1\nPRJNAME1=M1\nPRJNAME2=M2\n"
            "[M1]\nA=1\n[BAD\nB=2\n[M2]\nC=3\n", &c) == TRUE, L"malformed load TRUE");
        CFGMODEL* m1 = cfg_find_internal(&c, L"M1");
        CFGMODEL* m2 = cfg_find_internal(&c, L"M2");
        ok(m1 && wcscmp(cfg_get(m1, L"A"), L"1") == 0, L"malformed: M1.A kept");
        ok(m1 && cfg_get(m1, L"B") == NULL, L"malformed: B not in M1");
        ok(m2 && wcscmp(cfg_get(m2, L"C"), L"3") == 0, L"malformed: M2.C kept");

        ok(load_utf8(
            "[TOOLSINFO]\nPRJNAME1=M1\nPRJNAME2=M2\n"
            "[M1]\nA=1\n[M1]\nA=2\nB=3\n[M2]\nC=4\n", &c) == TRUE, L"dup load TRUE");
        ok(c.nmodels == 2, L"dup: nmodels 2");
        m1 = cfg_find_internal(&c, L"M1");
        ok(m1 && wcscmp(cfg_get(m1, L"A"), L"2") == 0, L"dup: A overwritten");
        ok(m1 && wcscmp(cfg_get(m1, L"B"), L"3") == 0, L"dup: B merged");

        /* 17 机型 → 16 */
        {
            char buf[4096]; int n = 0;
            n += snprintf(buf + n, sizeof(buf) - n, "[TOOLSINFO]\n");
            for (int i = 1; i <= 17; i++) n += snprintf(buf + n, sizeof(buf) - n, "PRJNAME%d=M%d\n", i, i);
            for (int i = 1; i <= 17; i++) n += snprintf(buf + n, sizeof(buf) - n, "[M%d]\nK=%d\n", i, i);
            ok(load_utf8(buf, &c) == TRUE, L"17 models load TRUE");
            ok(c.nmodels == CFG_MAX_MODELS, L"17 models truncated to 16");
        }
        /* 50 键 → 48 */
        {
            char buf[4096]; int n = 0;
            n += snprintf(buf + n, sizeof(buf) - n, "[TOOLSINFO]\nPRJNAME1=M1\n[M1]\n");
            for (int i = 1; i <= 50; i++) n += snprintf(buf + n, sizeof(buf) - n, "K%d=%d\n", i, i);
            ok(load_utf8(buf, &c) == TRUE, L"50 keys load TRUE");
            CFGMODEL* mm = cfg_find_internal(&c, L"M1");
            ok(mm && mm->nkeys == CFG_MAX_KEYS, L"50 keys truncated to 48");
        }
        /* 超长节名忽略，后续合法节恢复 */
        {
            char buf[512]; int n = 0;
            n += snprintf(buf + n, sizeof(buf) - n, "[TOOLSINFO]\nPRJNAME1=M1\n[");
            for (int i = 0; i < 80; i++) n += snprintf(buf + n, sizeof(buf) - n, "A");
            n += snprintf(buf + n, sizeof(buf) - n, "]\nK=1\n[M1]\nK=2\n");
            ok(load_utf8(buf, &c) == TRUE, L"long section load TRUE");
            ok(c.nmodels == 1, L"long section ignored");
            CFGMODEL* mm = cfg_find_internal(&c, L"M1");
            ok(mm && wcscmp(cfg_get(mm, L"K"), L"2") == 0, L"long section: next valid kept");
        }
        /* 空节名忽略 */
        ok(load_utf8("[TOOLSINFO]\nPRJNAME1=M1\n[]\nK=1\n[M1]\nK=2\n", &c) == TRUE, L"empty section load TRUE");
        ok(c.nmodels == 1 && wcscmp(cfg_get(cfg_find_internal(&c, L"M1"), L"K"), L"2") == 0, L"empty section ignored");

        /* API NULL 防御 */
        ok(cfg_get(NULL, L"A") == NULL, L"cfg_get NULL model");
        ok(cfg_get(cfg_find_internal(&c, L"M1"), NULL) == NULL, L"cfg_get NULL key");
        wchar_t obuf[8]; obuf[0] = 'X';
        cfg_get_def(NULL, L"A", L"D", obuf, 8);
        ok(obuf[0] == 0 || wcscmp(obuf, L"D") == 0, L"cfg_get_def NULL model safe (empty or default)");
        cfg_get_def(cfg_find_internal(&c, L"M1"), L"K", L"D", NULL, 0);
        ok(cfg_get_int(NULL, L"K", 7) == 7, L"cfg_get_int NULL -> default");
        ok(cfg_enabled(NULL, L"ENABLE_READ") == FALSE, L"cfg_enabled NULL -> FALSE");
        ok(cfg_find_internal(NULL, L"M1") == NULL, L"find_internal NULL");
        ok(cfg_find_display(NULL, L"M1") == NULL, L"find_display NULL");
    }

    /* ================= 4. 显示名大小写不敏感 ================= */
    {
        static CONFIG c;
        ok(load_utf8("[TOOLSINFO]\nPRJNAME1=M1\nDISPLAYNAME1=Alpha\n[M1]\nA=1\n", &c) == TRUE, L"display load");
        ok(cfg_find_display(&c, L"alpha") == cfg_find_internal(&c, L"M1"), L"find_display case-insens");
    }

    /* ================= 5. cfg_save_key 回归（真实 profiles 副本） ================= */
    {
        wchar_t src[MAX_PATH], dst[MAX_PATH];
        lstrcpynW(src, L".", MAX_PATH); lstrcatW(src, L"\\profiles\\UdiskTools.ini");
        lstrcpynW(dst, TDIR, MAX_PATH); lstrcatW(dst, L"\\prof_save.ini");
        CopyFileW(src, dst, FALSE);
        static CONFIG c;
        ok(cfg_load(dst, &c) == TRUE, L"prof copy load TRUE");
        ok(cfg_save_key(dst, L"GENERIC", L"READFILE", L"LEAD_TEST.TXT") == TRUE, L"prof save key TRUE");
        static CONFIG c2;
        ok(cfg_load(dst, &c2) == TRUE, L"prof reload TRUE");
        ok(wcscmp(cfg_get(cfg_find_internal(&c2, L"GENERIC"), L"READFILE"), L"LEAD_TEST.TXT") == 0, L"prof saved value");
        DWORD first_len = 0;
        char* first = rdall(dst, &first_len);
        BOOL stable = TRUE;
        for (int i = 0; i < 4; i++) {
            if (!cfg_save_key(dst, L"GENERIC", L"READFILE", L"LEAD_TEST.TXT")) stable = FALSE;
            DWORD n2 = 0; char* b2 = rdall(dst, &n2);
            if (!b2 || n2 != first_len || memcmp(b2, first, n2) != 0) stable = FALSE;
            free(b2);
        }
        ok(stable, L"prof save 5x byte-stable");
        free(first);
    }

    /* ================= 6. UTF-16BE / 无 BOM UTF-16 保存保编码 ================= */
    {
        /* UTF-16BE + BOM 文件 */
        const wchar_t* wini = L"[TOOLSINFO]\r\nPRJNAME1=M1\r\n[M1]\r\nK=1\r\n";
        int wl = (int)wcslen(wini);
        BYTE* buf = (BYTE*)malloc(wl * 2 + 2);
        buf[0] = 0xFE; buf[1] = 0xFF;
        for (int i = 0; i < wl; i++) {
            buf[2 + i * 2] = (BYTE)(wini[i] >> 8);
            buf[2 + i * 2 + 1] = (BYTE)(wini[i] & 0xFF);
        }
        wchar_t p[MAX_PATH]; lstrcpynW(p, TDIR, MAX_PATH); lstrcatW(p, L"\\be.ini");
        wrbytes(p, buf, wl * 2 + 2);
        static CONFIG c;
        ok(cfg_load(p, &c) == TRUE, L"utf16be ini load");
        ok(cfg_save_key(p, L"M1", L"K", L"2") == TRUE, L"utf16be save");
        DWORD n = 0; BYTE* rb = (BYTE*)rdall(p, &n);
        ok(rb && n >= 2 && rb[0] == 0xFE && rb[1] == 0xFF, L"utf16be BOM preserved");
        CONFIG c2; ok(cfg_load(p, &c2) == TRUE, L"utf16be reload");
        ok(wcscmp(cfg_get(cfg_find_internal(&c2, L"M1"), L"K"), L"2") == 0, L"utf16be saved value");
        free(rb); free(buf);
    }

    /* ================= 7. 默认配置内嵌一致性 ================= */
    {
        DWORD dn = 0, pn = 0;
        char* def = parse_default_ini(&dn);
        wchar_t p[MAX_PATH]; lstrcpynW(p, L".", MAX_PATH); lstrcatW(p, L"\\profiles\\UdiskTools_default.ini");
        char* prof = NULL;
        ok(def != NULL, L"parse default_ini.h");
        ok(normalize_file(p, &prof, &pn, TRUE) == TRUE, L"read profiles");
        if (def && prof) {
            ok(dn == pn, L"default_ini length == profiles normalized");
            ok(dn == pn && memcmp(def, prof, dn) == 0, L"default_ini content == profiles");
        }
        free(def); free(prof);
    }

    cleanup_dir();
    wprintf(L"\n[test_encoding] PASS=%d FAIL=%d\n", pass, fail);
    return fail ? 1 : 0;
}


