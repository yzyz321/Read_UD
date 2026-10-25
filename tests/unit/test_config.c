/* test_config.c - 配置读写/注释规则/编码往返单元测试（公开 API）
 * 来源：第 1 批 cfg_save_key + 5a 验收用例；所有写盘仅在 %TEMP% 自建目录并清理。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "util.h"

static int pass = 0, fail = 0;
static wchar_t BASE[MAX_PATH];

static void failmsg_(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(buf, 2047, fmt, ap);
    va_end(ap);
    buf[2047] = 0;
    wprintf(L"FAIL: %ls\n", buf);
}

#define CHK(cond, ...) do { \
    if (cond) pass++; \
    else { fail++; failmsg_(__VA_ARGS__); } \
} while (0)

static void full(const wchar_t* name, wchar_t* out) { lstrcpynW(out, BASE, MAX_PATH); lstrcatW(out, name); }

/* 在 %TEMP% 下建立本进程唯一目录（测试结束清理） */
static void init_temp_dir(void)
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) tmp[0] = 0;
    _snwprintf(BASE, MAX_PATH, L"%sudk_tests_config_%lu\\", tmp,
               (unsigned long)GetCurrentProcessId());
    BASE[MAX_PATH - 1] = 0;
    CreateDirectoryW(BASE, NULL);
}

static void cleanup_dir(void)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    lstrcpynW(pat, BASE, MAX_PATH);
    lstrcatW(pat, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t f[MAX_PATH];
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            lstrcpynW(f, BASE, MAX_PATH);
            lstrcatW(f, fd.cFileName);
            SetFileAttributesW(f, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(f);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(BASE);
}

static BOOL file_read_bytes(const wchar_t* path, char** buf, DWORD* len)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    *len = (DWORD)sz.QuadPart;
    *buf = (char*)malloc(*len + 1);
    DWORD rd = 0; ReadFile(h, *buf, *len, &rd, NULL); CloseHandle(h);
    (*buf)[rd] = 0; *len = rd; return TRUE;
}

static BOOL file_write_bytes(const wchar_t* path, const char* buf, DWORD len)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w = 0; BOOL ok = WriteFile(h, buf, len, &w, NULL) && w == len;
    CloseHandle(h); return ok;
}

static void copy_file(const wchar_t* src, const wchar_t* dst)
{
    DeleteFileW(dst);
    CopyFileW(src, dst, FALSE);
}

static int count_lines(const char* b, DWORD n)
{
    int c = 0;
    for (DWORD i = 0; i < n; i++) if (b[i] == '\n') c++;
    return c;
}

static int count_substr(const char* b, DWORD n, const char* sub)
{
    int c = 0; size_t sl = strlen(sub);
    for (DWORD i = 0; i + sl <= n; i++) if (memcmp(b + i, sub, sl) == 0) c++;
    return c;
}

/* 生成指定编码的 ini（内容为 UTF-8 文本） */
static BOOL write_encoded(const wchar_t* path, const char* utf8, int enc /*0=utf8,1=utf8bom,2=utf16le,3=gbk*/)
{
    wchar_t* w = utf8_to_wide(utf8, (int)strlen(utf8));
    if (!w) return FALSE;
    if (enc == 2) {
        HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) { free(w); return FALSE; }
        DWORD written = 0; WORD bom = 0xFEFF; BOOL ok = FALSE;
        if (WriteFile(h, &bom, 2, &written, NULL))
            ok = WriteFile(h, w, (DWORD)(wcslen(w) * sizeof(wchar_t)), &written, NULL);
        CloseHandle(h); free(w); return ok;
    }
    int cp = (enc == 3) ? 936 : CP_UTF8;
    int need = WideCharToMultiByte(cp, 0, w, -1, NULL, 0, NULL, NULL);   /* 含 NUL */
    int off = (enc == 1) ? 3 : 0;
    char* u = (char*)malloc(need + off);
    if (!u) { free(w); return FALSE; }
    if (off) { u[0] = (char)0xEF; u[1] = (char)0xBB; u[2] = (char)0xBF; }
    WideCharToMultiByte(cp, 0, w, -1, u + off, need, NULL, NULL);
    free(w);
    BOOL ok = file_write_bytes(path, u, (DWORD)(need - 1 + off));
    free(u);
    return ok;
}

int main(void)
{
    wchar_t ini[MAX_PATH], ini2[MAX_PATH], orig[MAX_PATH];
    init_temp_dir();   /* 先建立 %TEMP% 目录，再拼路径（否则会写到 cwd） */
    full(L"a.ini", ini); full(L"b.ini", ini2); full(L"orig.ini", orig);

    const char* origText =
        "; ============================================================\r\n"
        "; UdiskTools test ini\r\n"
        "; ============================================================\r\n"
        "[TOOLSINFO]\r\n"
        "TITLE=测试标题\r\n"
        "VERSION=1.0.0\r\n"
        "ADMINPASS= 1234                     ; 管理员密码\r\n"
        "PRJNAME1=M1\r\n"
        "DISPLAYNAME1=机型一\r\n"
        "PRJNAME2=M2\r\n"
        "DISPLAYNAME2=机型二\r\n"
        "DEFAULTMODEL=M1\r\n"
        "[M1]\r\n"
        "ENABLE_READ=1\r\n"
        "ENABLE_WRITE=1\r\n"
        "K1=old1 ; 保留的注释\r\n"
        "READRULE=MUST_CONTAIN:8685;MIN_LEN:10\r\n"
        "SRCPATH=.\\upgrade\\*.*          ; 源目录\r\n"
        "[M2]\r\n"
        "ENABLE_READ=0\r\n"
        "K2=old2\r\n";

    /* ---------- 1. 基础保存：目标行改变，其余字节不变，无崩溃 ---------- */
    file_write_bytes(orig, origText, (DWORD)strlen(origText));
    copy_file(orig, ini);
    static CONFIG cfg;
    CHK(cfg_load(ini, &cfg) == TRUE, L"load original failed");
    CHK(cfg.nmodels == 2, L"nmodels=%d expect 2", cfg.nmodels);
    CHK(cfg_save_key(ini, L"M1", L"K1", L"new1") == TRUE, L"save K1 failed");
    {
        char *b0, *b1; DWORD n0, n1;
        file_read_bytes(orig, &b0, &n0); file_read_bytes(ini, &b1, &n1);
        CHK(n0 == n1, L"file size changed %lu -> %lu", (unsigned long)n0, (unsigned long)n1);
        CHK(count_substr(b1, n1, "K1=new1") == 1, L"K1=new1 missing");
        CHK(count_substr(b1, n1, "; 保留的注释") == 1, L"inline comment lost");
        CHK(count_substr(b1, n1, "SRCPATH=.\\upgrade\\*.*          ; 源目录") == 1, L"other line changed");
        CHK(count_substr(b1, n1, "\r\r") == 0, L"CRCRLF detected after save");
        /* 除目标行外逐字节比较：把 target 行替换为空再比 */
        int same_others = 1; DWORD i = 0, j = 0;
        while (i < n0 && j < n1) {
            if (b0[i] == 'K' && b0[i+1] == '1' && b0[i+2] == '=') { while (i < n0 && b0[i] != '\n') i++; i++; }
            else if (b1[j] == 'K' && b1[j+1] == '1' && b1[j+2] == '=') { while (j < n1 && b1[j] != '\n') j++; j++; }
            else { if (b0[i] != b1[j]) { same_others = 0; break; } i++; j++; }
        }
        CHK(same_others && i >= n0 && j >= n1, L"other bytes changed");
        free(b0); free(b1);
    }
    static CONFIG cfg2;
    CHK(cfg_load(ini, &cfg2) == TRUE, L"reload after save failed");
    {
        CFGMODEL* m = cfg_find_internal(&cfg2, L"M1");
        const wchar_t* v = m ? cfg_get(m, L"K1") : NULL;
        CHK(v && wcscmp(v, L"new1") == 0, L"reload K1=%ls", v ? v : L"(null)");
        CHK(wcscmp(cfg2.adminpass, L"1234") == 0, L"adminpass=%ls", cfg2.adminpass);
    }

    /* ---------- 2. 连续保存 5 次字节稳定 ---------- */
    {
        char* first = NULL; DWORD fn = 0; int stable = 1;
        for (int i = 0; i < 5; i++) {
            if (!cfg_save_key(ini, L"M1", L"K1", L"new1")) { stable = 0; break; }
            char* b; DWORD n;
            file_read_bytes(ini, &b, &n);
            if (i == 0) { first = b; fn = n; }
            else { if (n != fn || memcmp(b, first, n) != 0) stable = 0; free(b); }
        }
        CHK(stable, L"file not byte-stable across 5 saves (%lu bytes)", (unsigned long)fn);
        free(first);
    }

    /* ---------- 3. 新增键插入正确节 ---------- */
    copy_file(orig, ini);
    CHK(cfg_save_key(ini, L"M1", L"NEWKEY", L"9") == TRUE, L"insert NEWKEY failed");
    CHK(cfg_load(ini, &cfg) == TRUE, L"reload after insert failed");
    {
        CFGMODEL* m1 = cfg_find_internal(&cfg, L"M1");
        CFGMODEL* m2 = cfg_find_internal(&cfg, L"M2");
        const wchar_t* a = m1 ? cfg_get(m1, L"NEWKEY") : NULL;
        const wchar_t* b = m2 ? cfg_get(m2, L"NEWKEY") : NULL;
        CHK(a && wcscmp(a, L"9") == 0, L"M1.NEWKEY=%ls", a ? a : L"(null)");
        CHK(b == NULL, L"M2.NEWKEY should be absent");
    }
    CHK(cfg_save_key(ini, L"NOSUCH", L"X", L"1") == FALSE, L"missing section should FALSE");
    CHK(cfg_save_key(ini, L"M1", L"BAD\r\nKEY", L"1") == FALSE, L"CRLF key should FALSE");
    CHK(cfg_save_key(ini, L"M1", L"K1", L"a\r\nb") == FALSE, L"CRLF value should FALSE");

    /* ---------- 4. 长值 600 字符：拒绝（>=512）不截断；400 字符接受且完整 ---------- */
    {
        wchar_t longv[700];
        for (int i = 0; i < 600; i++) longv[i] = (wchar_t)(L'a' + i % 26);
        longv[600] = 0;
        CHK(cfg_save_key(ini, L"M1", L"K1", longv) == FALSE, L"600-char value should be rejected");
        longv[400] = 0;
        CHK(cfg_save_key(ini, L"M2", L"K2", longv) == TRUE, L"400-char value save failed");
        static CONFIG c3;
        cfg_load(ini, &c3);
        CFGMODEL* m = cfg_find_internal(&c3, L"M2");
        const wchar_t* v = m ? cfg_get(m, L"K2") : NULL;
        CHK(v && wcscmp(v, longv) == 0, L"400-char value round-trip failed (len=%d)", v ? (int)wcslen(v) : -1);
    }

    /* ---------- 5. 四种编码往返：编码头保持、内容可读 ---------- */
    {
        const char* text =
            "[TOOLSINFO]\r\nPRJNAME1=E1\r\nDISPLAYNAME1=编码一\r\nDEFAULTMODEL=E1\r\n"
            "[E1]\r\nENABLE_READ=1\r\nK=old\r\n";
        int encs[4] = { 0, 1, 2, 3 };
        const wchar_t* names[4] = { L"u8.ini", L"u8bom.ini", L"u16.ini", L"gbk.ini" };
        for (int e = 0; e < 4; e++) {
            wchar_t p[MAX_PATH]; full(names[e], p);
            CHK(write_encoded(p, text, encs[e]) == TRUE, L"write enc %d failed", e);
            CHK(cfg_save_key(p, L"E1", L"K", L"new") == TRUE, L"save enc %d failed", e);
            char* b; DWORD n;
            file_read_bytes(p, &b, &n);
            if (encs[e] == 2) CHK(n >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE, L"utf16 BOM lost");
            if (encs[e] == 1) CHK(n >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF, L"utf8 BOM lost");
            if (encs[e] == 0) CHK(!(n >= 3 && (unsigned char)b[0] == 0xEF), L"utf8 no-BOM got BOM");
            free(b);
            CONFIG c4;
            CHK(cfg_load(p, &c4) == TRUE, L"reload enc %d failed", e);
            CFGMODEL* mm = cfg_find_internal(&c4, L"E1");
            const wchar_t* vv = mm ? cfg_get(mm, L"K") : NULL;
            CHK(vv && wcscmp(vv, L"new") == 0, L"enc %d value round-trip", e);
            CHK(wcscmp(c4.models[0].display, L"编码一") == 0, L"enc %d display corrupted", e);
        }
    }

    /* ---------- 6. READRULE 分号链（item 5a） ---------- */
    copy_file(orig, ini);
    CHK(cfg_load(ini, &cfg) == TRUE, L"load for READRULE failed");
    {
        CFGMODEL* m = cfg_find_internal(&cfg, L"M1");
        const wchar_t* v = m ? cfg_get(m, L"READRULE") : NULL;
        CHK(v && wcscmp(v, L"MUST_CONTAIN:8685;MIN_LEN:10") == 0, L"READRULE truncated: %ls", v ? v : L"(null)");
        const wchar_t* ad = cfg_get(m, L"K1");
        CHK(ad && wcscmp(ad, L"old1") == 0, L"inline comment broke value: %ls", ad ? ad : L"(null)");
        const wchar_t* sp = cfg_get(m, L"SRCPATH");
        CHK(sp && wcscmp(sp, L".\\upgrade\\*.*") == 0, L"SRCPATH value changed: %ls", sp ? sp : L"(null)");
    }
    /* 值含分号但前无空白：保存后 round-trip */
    CHK(cfg_save_key(ini, L"M1", L"R2", L"a;b;c") == TRUE, L"save ;value failed");
    CHK(cfg_load(ini, &cfg) == TRUE, L"reload ;value failed");
    {
        CFGMODEL* m = cfg_find_internal(&cfg, L"M1");
        const wchar_t* v = m ? cfg_get(m, L"R2") : NULL;
        CHK(v && wcscmp(v, L"a;b;c") == 0, L";value round-trip: %ls", v ? v : L"(null)");
    }

    /* ---------- 7. 行尾风格保持（LF 文件不变成 CRLF） ---------- */
    {
        const char* lfText = "[TOOLSINFO]\nPRJNAME1=L1\nDISPLAYNAME1=L1\nDEFAULTMODEL=L1\n[L1]\nK=v\n";
        wchar_t p[MAX_PATH]; full(L"lf.ini", p);
        file_write_bytes(p, lfText, (DWORD)strlen(lfText));
        CHK(cfg_save_key(p, L"L1", L"K", L"v2") == TRUE, L"LF save failed");
        char* b; DWORD n; file_read_bytes(p, &b, &n);
        CHK(count_substr(b, n, "\r") == 0, L"LF file got CR (%d)", count_substr(b, n, "\r"));
        CHK(count_lines(b, n) == 6, L"LF line count %d", count_lines(b, n));
        free(b);
    }

    /* ---------- 8. 只读文件：保存失败且原文件不变 ---------- */
    {
        copy_file(orig, ini);
        char *before; DWORD bn;
        file_read_bytes(ini, &before, &bn);
        SetFileAttributesW(ini, FILE_ATTRIBUTE_READONLY);
        CHK(cfg_save_key(ini, L"M1", L"K1", L"zzz") == FALSE, L"readonly save should FALSE");
        SetFileAttributesW(ini, FILE_ATTRIBUTE_NORMAL);
        char* after; DWORD an;
        file_read_bytes(ini, &after, &an);
        CHK(an == bn && memcmp(before, after, an) == 0, L"readonly file modified");
        free(before); free(after);
    }

    /* ---------- 9. 属性/时间戳保留 ---------- */
    {
        copy_file(orig, ini);
        SetFileAttributesW(ini, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_ARCHIVE);
        FILETIME ft; GetFileTime(NULL, NULL, NULL, &ft);
        /* 设置一个明显的旧修改时间 */
        SYSTEMTIME st; memset(&st, 0, sizeof(st));
        st.wYear = 2020; st.wMonth = 1; st.wDay = 2; st.wHour = 3;
        FILETIME oldft; SystemTimeToFileTime(&st, &oldft);
        HANDLE hf = CreateFileW(ini, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
        SetFileTime(hf, NULL, NULL, &oldft); CloseHandle(hf);
        CHK(cfg_save_key(ini, L"M1", L"K1", L"attr") == TRUE, L"save attr test failed");
        DWORD attr = GetFileAttributesW(ini);
        CHK((attr & FILE_ATTRIBUTE_HIDDEN) != 0, L"hidden attribute lost (attr=0x%08lX)", attr);
        hf = CreateFileW(ini, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        FILETIME cur; GetFileTime(hf, NULL, NULL, &cur); CloseHandle(hf);
        CHK(CompareFileTime(&cur, &oldft) == 0, L"mtime not preserved");
        SetFileAttributesW(ini, FILE_ATTRIBUTE_NORMAL);
    }

    cleanup_dir();
    wprintf(L"\n[test_config] PASS=%d FAIL=%d\n", pass, fail);
    return fail ? 1 : 0;
}



