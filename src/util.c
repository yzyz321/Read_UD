/* ============================================================
 * util.c - 通用工具实现
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"
#include "md5.h"

/* ================= 编码转换 ================= */

wchar_t* utf8_to_wide(const char* s, int len)
{
    int n;
    wchar_t* w;
    if (!s) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    if (len < 0) len = (int)strlen(s);
    if (len == 0) {
        w = (wchar_t*)calloc(1, sizeof(wchar_t));
        return w;
    }
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, NULL, 0);
    if (n <= 0) return NULL;   /* 严格 UTF-8：不再静默回退 GBK */
    w = (wchar_t*)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, w, n) != n) {
        free(w);
        return NULL;
    }
    w[n] = 0;
    return w;
}

wchar_t* ansi_to_wide(const char* s, int len)
{
    if (!s) return NULL;
    if (len < 0) len = (int)strlen(s);
    int n = MultiByteToWideChar(CP_ACP, 0, s, len, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t* w = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_ACP, 0, s, len, w, n);
    w[n] = 0;
    return w;
}

int wide_to_utf8(const wchar_t* s, char* out, int outlen)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, out, outlen, NULL, NULL);
    return n > 0 ? n - 1 : -1;
}

/* ================= 统一文本编码探测/解码 =================
 * 供 config.c（ini 读取/保存）与 read_file_text 共用：
 *  - BOM：EF BB BF / FF FE / FE FF；
 *  - 无 BOM：零字节分布启发式识别 UTF-16LE/BE（识破无 BOM UTF-16）；
 *  - 严格 UTF-8；失败后区分“损坏的 UTF-8”（拒绝，不静默按 GBK 解码）
 *    与 GBK/ANSI；
 *  - 解码结果含内嵌 U+0000 → 按二进制拒绝（避免宽串截断风险）。
 * ============================================================ */

const wchar_t* text_enc_name(TEXTENC enc)
{
    switch (enc) {
    case TEXT_ENC_UTF8:     return L"UTF-8";
    case TEXT_ENC_UTF8_BOM: return L"UTF-8(BOM)";
    case TEXT_ENC_UTF16LE:  return L"UTF-16LE";
    case TEXT_ENC_UTF16BE:  return L"UTF-16BE";
    case TEXT_ENC_ANSI:     return L"ANSI(CP936)";
    default:                return L"unknown";
    }
}

static void td_set_err(wchar_t* err, size_t errlen, const wchar_t* fmt, ...)
{
    va_list ap;
    if (!err || errlen == 0) return;
    va_start(ap, fmt);
    _vsnwprintf(err, errlen - 1, fmt, ap);
    va_end(ap);
    err[errlen - 1] = 0;
}

/* 多字节编码（UTF-8/CP936）严格解码；成功时 *out_nch 为宽字符数 */
static BOOL td_multibyte_decode(UINT cp, DWORD flags, const BYTE* b, size_t len,
                                wchar_t** out, size_t* out_nch)
{
    int n;
    wchar_t* w;

    *out = NULL;
    if (out_nch) *out_nch = 0;
    if (len == 0) {
        w = (wchar_t*)calloc(1, sizeof(wchar_t));
        if (!w) return FALSE;
        *out = w;
        return TRUE;
    }
    if (len > 0x7FFFFFFFu) return FALSE;
    n = MultiByteToWideChar(cp, flags, (const char*)b, (int)len, NULL, 0);
    if (n <= 0) return FALSE;
    w = (wchar_t*)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w) return FALSE;
    if (MultiByteToWideChar(cp, flags, (const char*)b, (int)len, w, n) != n) {
        free(w);
        return FALSE;
    }
    w[n] = 0;
    *out = w;
    if (out_nch) *out_nch = (size_t)n;
    return TRUE;
}

static BOOL td_has_nul(const wchar_t* w, size_t nch)
{
    size_t i;
    for (i = 0; i < nch; i++)
        if (w[i] == 0) return TRUE;
    return FALSE;
}

/* UTF-16 → 宽字符（Windows wchar_t 为 2 字节）。
 * 返回 0=成功，1=长度为奇数，2=含 U+0000，3=非法代理对，4=内存不足。 */
static int td_utf16_decode(const BYTE* b, size_t len, BOOL be, wchar_t** out)
{
    size_t nch, i;
    wchar_t* w;

    *out = NULL;
    if ((len & 1u) != 0) return 1;
    nch = len / 2;
    w = (wchar_t*)malloc((nch + 1) * sizeof(wchar_t));
    if (!w) return 4;
    for (i = 0; i < nch; i++) {
        unsigned int u = be
            ? (((unsigned int)b[2 * i] << 8) | (unsigned int)b[2 * i + 1])
            : (((unsigned int)b[2 * i + 1] << 8) | (unsigned int)b[2 * i]);
        if (u == 0) { free(w); return 2; }
        w[i] = (wchar_t)u;
    }
    w[nch] = 0;
    for (i = 0; i < nch; i++) {
        unsigned int u = (unsigned int)w[i];
        if (u >= 0xD800 && u <= 0xDBFF) {
            unsigned int u2;
            if (i + 1 >= nch) { free(w); return 3; }
            u2 = (unsigned int)w[i + 1];
            if (u2 < 0xDC00 || u2 > 0xDFFF) { free(w); return 3; }
            i++;
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            free(w);
            return 3;
        }
    }
    *out = w;
    return 0;
}

static BOOL td_utf16_report(wchar_t* err, size_t errlen, const wchar_t* label, int rc)
{
    switch (rc) {
    case 1:
        td_set_err(err, errlen, L"%s 长度为奇数，无法按 UTF-16 解码", label);
        break;
    case 2:
        td_set_err(err, errlen, L"%s 文本包含内嵌 NUL（U+0000），疑似二进制", label);
        break;
    case 3:
        td_set_err(err, errlen, L"%s 存在非法 UTF-16 代理对", label);
        break;
    default:
        td_set_err(err, errlen, L"%s 解码失败（内存不足）", label);
        break;
    }
    return FALSE;
}

/* 扫描 UTF-8 统计：首个非法字节偏移/字节值、非法字节数、合法多字节序列数。
 * 仅用于“损坏的 UTF-8”与 GBK 的区分，最终解码仍以 MultiByteToWideChar 为准。 */
static void td_scan_utf8(const BYTE* b, size_t len, size_t* first_bad,
                         size_t* bad_bytes, size_t* multi_seqs, BYTE* bad_byte)
{
    size_t i = 0;

    *first_bad = (size_t)-1;
    *bad_bytes = 0;
    *multi_seqs = 0;
    *bad_byte = 0;
    while (i < len) {
        BYTE c = b[i];
        size_t need, k;
        BYTE lo = 0x80, hi = 0xBF;
        BOOL ok = TRUE;

        if (c < 0x80) { i++; continue; }
        if (c >= 0xC2 && c <= 0xDF) {
            need = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            need = 2;
            if (c == 0xE0) lo = 0xA0;
            else if (c == 0xED) hi = 0x9F;
        } else if (c >= 0xF0 && c <= 0xF4) {
            need = 3;
            if (c == 0xF0) lo = 0x90;
            else if (c == 0xF4) hi = 0x8F;
        } else {
            if (*first_bad == (size_t)-1) { *first_bad = i; *bad_byte = c; }
            (*bad_bytes)++;
            i++;
            continue;
        }
        for (k = 1; k <= need; k++) {
            BYTE l = (k == 1) ? lo : 0x80;
            BYTE h = (k == 1) ? hi : 0xBF;
            if (i + k >= len || b[i + k] < l || b[i + k] > h) { ok = FALSE; break; }
        }
        if (!ok) {
            if (*first_bad == (size_t)-1) { *first_bad = i; *bad_byte = c; }
            (*bad_bytes)++;
            i++;
            continue;
        }
        (*multi_seqs)++;
        i += need + 1;
    }
}

BOOL text_decode(const void* data, size_t len, wchar_t** out, TEXTENC* enc,
                 wchar_t* err, size_t errlen)
{
    const BYTE* b = (const BYTE*)data;
    wchar_t* w = NULL;

    if (err && errlen) err[0] = 0;
    if (!out) return FALSE;
    *out = NULL;
    if (enc) *enc = TEXT_ENC_UTF8;
    if (!data && len > 0) {
        td_set_err(err, errlen, L"输入数据为空指针");
        return FALSE;
    }
    if (len == 0) {
        w = (wchar_t*)calloc(1, sizeof(wchar_t));
        if (!w) { td_set_err(err, errlen, L"内存不足"); return FALSE; }
        *out = w;
        return TRUE;
    }

    /* 1) BOM */
    if (len >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
        size_t nch = 0;
        if (!td_multibyte_decode(CP_UTF8, MB_ERR_INVALID_CHARS, b + 3, len - 3, &w, &nch)) {
            td_set_err(err, errlen, L"UTF-8(BOM) 解码失败：存在非法 UTF-8 字节");
            return FALSE;
        }
        if (td_has_nul(w, nch)) {
            free(w);
            td_set_err(err, errlen, L"文本包含内嵌 NUL，疑似二进制");
            return FALSE;
        }
        *out = w;
        if (enc) *enc = TEXT_ENC_UTF8_BOM;
        return TRUE;
    }
    if (len >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        int rc = td_utf16_decode(b + 2, len - 2, FALSE, &w);
        if (rc != 0) return td_utf16_report(err, errlen, L"UTF-16LE(BOM)", rc);
        *out = w;
        if (enc) *enc = TEXT_ENC_UTF16LE;
        return TRUE;
    }
    if (len >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
        int rc = td_utf16_decode(b + 2, len - 2, TRUE, &w);
        if (rc != 0) return td_utf16_report(err, errlen, L"UTF-16BE(BOM)", rc);
        *out = w;
        if (enc) *enc = TEXT_ENC_UTF16BE;
        return TRUE;
    }

    /* 2) 无 BOM 且含 0x00：零字节分布启发式判断无 BOM UTF-16 */
    if (memchr(b, 0, len) != NULL) {
        size_t z_even = 0, z_odd = 0, i;
        size_t even_total = (len + 1) / 2;
        size_t odd_total = len / 2;
        double re, ro;

        for (i = 0; i < len; i++) {
            if (b[i] != 0) continue;
            if ((i & 1u) == 0) z_even++;
            else z_odd++;
        }
        re = even_total ? (double)z_even / (double)even_total : 0.0;
        ro = odd_total ? (double)z_odd / (double)odd_total : 0.0;
        if (ro > 0.3 && ro > re * 2.0) {          /* 零字节集中在奇数位 → LE */
            int rc = td_utf16_decode(b, len, FALSE, &w);
            if (rc != 0) return td_utf16_report(err, errlen, L"疑似 UTF-16LE", rc);
            *out = w;
            if (enc) *enc = TEXT_ENC_UTF16LE;
            return TRUE;
        }
        if (re > 0.3 && re > ro * 2.0) {          /* 零字节集中在偶数位 → BE */
            int rc = td_utf16_decode(b, len, TRUE, &w);
            if (rc != 0) return td_utf16_report(err, errlen, L"疑似 UTF-16BE", rc);
            *out = w;
            if (enc) *enc = TEXT_ENC_UTF16BE;
            return TRUE;
        }
        td_set_err(err, errlen, L"包含 NUL 字节且无 UTF-16 零字节规律，疑似二进制文件");
        return FALSE;
    }

    /* 3) 无 BOM 无 NUL：严格 UTF-8 */
    {
        size_t nch = 0;
        if (td_multibyte_decode(CP_UTF8, MB_ERR_INVALID_CHARS, b, len, &w, &nch)) {
            if (td_has_nul(w, nch)) {
                free(w);
                td_set_err(err, errlen, L"文本包含内嵌 NUL，疑似二进制");
                return FALSE;
            }
            *out = w;
            if (enc) *enc = TEXT_ENC_UTF8;
            return TRUE;
        }
    }

    /* 4) UTF-8 失败：区分“损坏的 UTF-8”与 GBK/ANSI */
    {
        size_t first_bad = (size_t)-1, bad_bytes = 0, multi_seqs = 0;
        BYTE bad_byte = 0;
        BOOL few_bad;
        td_scan_utf8(b, len, &first_bad, &bad_bytes, &multi_seqs, &bad_byte);
        few_bad = (bad_bytes <= 2) || (bad_bytes * 100 < len);
        if (multi_seqs > 0 && few_bad) {
            td_set_err(err, errlen,
                       L"疑似 UTF-8 但存在非法字节（偏移 %llu，错误字节 0x%02X，共 %llu 个非法字节）",
                       (unsigned long long)first_bad, (unsigned int)bad_byte,
                       (unsigned long long)bad_bytes);
            return FALSE;   /* 不静默按 GBK 解码 */
        }
    }
    {
        size_t nch = 0;
        if (td_multibyte_decode(936, 0, b, len, &w, &nch)) {
            *out = w;
            if (enc) *enc = TEXT_ENC_ANSI;
            return TRUE;
        }
    }
    td_set_err(err, errlen, L"无法识别文本编码（UTF-8 非法且 GBK/CP936 解码失败）");
    return FALSE;
}

/* ================= 字符串辅助 ================= */

wchar_t* wcs_trim(wchar_t* s)
{
    while (*s == L' ' || *s == L'\t' || *s == L'\r') s++;
    wchar_t* e = s + wcslen(s);
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r' || e[-1] == L'\n')) e--;
    *e = 0;
    return s;
}

wchar_t* wcs_dup(const wchar_t* s)
{
    size_t n = wcslen(s);
    wchar_t* p = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
    if (p) memcpy(p, s, (n + 1) * sizeof(wchar_t));
    return p;
}

wchar_t* exe_dir(void)
{
    static wchar_t dir[MAX_PATH];
    wchar_t full[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, full, MAX_PATH);
    (void)n;
    wchar_t* slash = wcsrchr(full, L'\\');
    if (slash) *slash = 0;
    lstrcpynW(dir, full, MAX_PATH);
    return dir;
}

void path_join(wchar_t* out, size_t outlen, const wchar_t* dir, const wchar_t* name)
{
    if (!dir || !dir[0]) lstrcpynW(out, name, (int)outlen);
    else if (dir[wcslen(dir) - 1] == L'\\') _snwprintf(out, outlen, L"%s%s", dir, name);
    else _snwprintf(out, outlen, L"%s\\%s", dir, name);
    out[outlen - 1] = 0;
}

/* 递归创建目录链：逐级 CreateDirectoryW，已存在忽略 */
BOOL util_ensure_dirs(const wchar_t* path)
{
    wchar_t tmp[MAX_PATH];
    lstrcpynW(tmp, path, MAX_PATH);
    size_t n = wcslen(tmp);
    while (n > 0 && (tmp[n - 1] == L'\\')) tmp[--n] = 0;   /* 去尾部反斜杠 */
    if (n < 3) return TRUE;                                /* 盘符根目录视为存在 */

    for (size_t i = 3; i <= n; i++) {                      /* 跳过 "X:\" */
        if (tmp[i] == L'\\' || tmp[i] == 0) {
            wchar_t save = tmp[i];
            tmp[i] = 0;
            if (!CreateDirectoryW(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                /* 目录被文件占用等情况：再探测一次 */
                DWORD attr = GetFileAttributesW(tmp);
                if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
                    return FALSE;
            }
            tmp[i] = save;
        }
    }
    return TRUE;
}

/* ================= 日志 ================= */

static HWND g_hwndGui = NULL;           /* 主窗口（接收 WM_APP_LOG） */

/* 自定义消息（与 gui.h 保持一致） */
#define WM_APP_LOG      (WM_APP + 1)

/* ---------- 日志临界区：INIT_ONCE 懒初始化 ----------
 * 旧实现用 `if (!g_cs_init) { InitializeCriticalSection(); g_cs_init = TRUE; }`
 * 存在两个线程同时进入时重复初始化/看到半初始化对象的数据竞争。 */
static INIT_ONCE g_log_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION g_logcs;
static BOOL g_logcs_ok = FALSE;

static BOOL CALLBACK log_once_cb(PINIT_ONCE once, PVOID param, PVOID* ctx)
{
    (void)once;
    (void)param;
    (void)ctx;
    if (InitializeCriticalSectionEx(&g_logcs, 0, 0))
        g_logcs_ok = TRUE;
    else
        OutputDebugStringW(L"[UdiskTools] 日志临界区初始化失败：文件日志仅输出到调试器\n");
    return TRUE;    /* 初始化只执行一次；失败也不再重试，靠 OutputDebugString 兜底 */
}

static void log_cs_init(void)
{
    InitOnceExecuteOnce(&g_log_once, log_once_cb, NULL, NULL);
}

/* ---------- 日志文件：按天缓存句柄、8MB 轮转、备用文件兜底 ---------- */
#define LOG_MAX_BYTES   (8LL * 1024 * 1024)

typedef struct {
    HANDLE  h;              /* 当前缓存句柄（锁内使用） */
    wchar_t path[MAX_PATH]; /* 当前文件完整路径（用于检测外部删除/移动） */
    wchar_t day[16];        /* "YYYYMMDD"（跨日判断） */
    BOOL    alt;            /* TRUE=当前是 logs\YYYYMMDD_alt.log 备用文件 */
    BOOL    need_bom;       /* 文件为空时首行前补 UTF-8 BOM */
    DWORD   last_error;     /* 最近一次打开/写入失败的错误码（诊断用） */
} LOGFILE;

static LOGFILE g_logf;      /* 仅在 g_logcs 锁内访问 */

void log_set_gui_hwnd(HWND hwnd) { g_hwndGui = hwnd; }

static const wchar_t* lv_tag(LOGLEVEL lv)
{
    switch (lv) {
    case LOG_OK:   return L"OK  ";
    case LOG_WARN: return L"WARN";
    case LOG_ERR:  return L"ERR ";
    case LOG_UI:   return L"UI  ";
    default:       return L"INFO";
    }
}

/* 关闭缓存句柄（锁内调用；可重复调用） */
static void log_close_locked(void)
{
    if (g_logf.h) CloseHandle(g_logf.h);
    memset(&g_logf, 0, sizeof(g_logf));
}

/* 打开指定日志文件（主/备用）；文件 >8MB 时先轮转为 <path>.1 再重开。
 * 成功返回句柄并把完整路径写入 outpath；返回 NULL 表示打开失败。必须持锁调用。 */
static HANDLE log_open_file_locked(const wchar_t* day, BOOL alt, BOOL* need_bom,
                                   wchar_t* outpath, size_t outcap)
{
    wchar_t dir[MAX_PATH];
    wchar_t name[MAX_PATH];
    wchar_t path[MAX_PATH];
    wchar_t rot[MAX_PATH];
    HANDLE h;
    LARGE_INTEGER sz;

    path_join(dir, MAX_PATH, exe_dir(), L"logs");
    CreateDirectoryW(dir, NULL);
    if (alt)
        _snwprintf(name, MAX_PATH, L"%s_alt.log", day);
    else
        _snwprintf(name, MAX_PATH, L"%s.log", day);
    name[MAX_PATH - 1] = 0;
    path_join(path, MAX_PATH, dir, name);
    _snwprintf(rot, MAX_PATH, L"%s.1", path);
    rot[MAX_PATH - 1] = 0;

    h = CreateFileW(path, FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx(h, &sz)) {
        CloseHandle(h);
        return NULL;
    }
    if (sz.QuadPart > LOG_MAX_BYTES) {          /* P3-5：单文件 >8MB 轮转 */
        CloseHandle(h);
        DeleteFileW(rot);                        /* 覆盖旧 .1 */
        MoveFileExW(path, rot, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        h = CreateFileW(path, FILE_APPEND_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return NULL;
        if (!GetFileSizeEx(h, &sz)) {
            CloseHandle(h);
            return NULL;
        }
        /* 若轮转失败（文件被占用等），继续追加原文件，避免丢日志 */
    }
    *need_bom = (sz.QuadPart == 0);
    if (outpath && outcap) lstrcpynW(outpath, path, (int)outcap);
    return h;
}

/* 打开并缓存主/备用文件（锁内调用） */
static BOOL log_cache_open_locked(const wchar_t* day, BOOL alt)
{
    BOOL need_bom = FALSE;
    wchar_t path[MAX_PATH] = L"";
    HANDLE h = log_open_file_locked(day, alt, &need_bom, path, MAX_PATH);
    if (h == NULL) return FALSE;
    g_logf.h = h;
    g_logf.alt = alt;
    g_logf.need_bom = need_bom;
    lstrcpynW(g_logf.path, path, MAX_PATH);
    lstrcpynW(g_logf.day, day, 16);
    return TRUE;
}

/* 向缓存句柄写一行 UTF-8（必要时补 BOM）；锁内调用 */
static BOOL log_write_locked(const char* utf8, int n)
{
    static const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
    DWORD written = 0;

    if (g_logf.need_bom) {
        if (!WriteFile(g_logf.h, bom, 3, &written, NULL) || written != 3)
            return FALSE;
        g_logf.need_bom = FALSE;
    }
    if (n > 0) {
        if (!WriteFile(g_logf.h, utf8, (DWORD)n, &written, NULL) ||
            written != (DWORD)n)
            return FALSE;
    }
    return TRUE;
}

/* 关闭日志文件缓存句柄（程序退出前调用；可重复调用安全） */
void log_shutdown(void)
{
    log_cs_init();
    if (!g_logcs_ok) return;
    EnterCriticalSection(&g_logcs);
    log_close_locked();
    LeaveCriticalSection(&g_logcs);
}

/* 写文件日志：exe_dir\logs\YYYYMMDD.log（强制，不可关闭）。
 * - 当日句柄缓存复用，避免每行 CreateFile/Close；
 * - 跨日自动切换；运行中 >8MB 轮转为 .log.1；
 * - 打开失败：OutputDebugStringW 兜底 + 尝试 logs\YYYYMMDD_alt.log；
 * - 写入失败：关闭句柄、OutputDebugStringW 兜底并重试一次备用文件；
 * - 任何失败都不会阻塞 applog 的 GUI 消息投递。 */
void filelog(LOGLEVEL lv, const wchar_t* fmt, ...)
{
    wchar_t line[2048];
    wchar_t full[2300];
    char utf8[4600];
    wchar_t day[16];
    __time64_t t;
    struct tm lt;
    va_list ap;
    int n;

    if (!fmt) fmt = L"";
    va_start(ap, fmt);
    _vsnwprintf(line, 2040, fmt, ap);
    va_end(ap);
    line[2040] = 0;

    t = _time64(NULL);
    _localtime64_s(&lt, &t);
    _snwprintf(day, 16, L"%04d%02d%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
    day[15] = 0;
    _snwprintf(full, 2300, L"[%02d:%02d:%02d][%s] %s\r\n",
               lt.tm_hour, lt.tm_min, lt.tm_sec, lv_tag(lv), line);
    full[2299] = 0;

    n = wide_to_utf8(full, utf8, 4600);
    if (n <= 0) {
        OutputDebugStringW(full);          /* 编码失败也不静默丢行 */
        return;
    }

    log_cs_init();
    if (!g_logcs_ok) {
        OutputDebugStringW(full);
        return;
    }
    EnterCriticalSection(&g_logcs);

    /* 跨日，或上次落在备用文件 → 关闭缓存，优先重试主文件 */
    if (g_logf.h && (g_logf.alt || wcscmp(g_logf.day, day) != 0))
        log_close_locked();

    /* 外部删除/移走日志文件：缓存句柄会写进已删除文件（静默丢行）→ 重新打开 */
    if (g_logf.h && g_logf.path[0] &&
        GetFileAttributesW(g_logf.path) == INVALID_FILE_ATTRIBUTES)
        log_close_locked();

    /* 运行中超过 8MB：关闭后重开会触发 .1 轮转 */
    if (g_logf.h) {
        LARGE_INTEGER sz;
        if (GetFileSizeEx(g_logf.h, &sz) && sz.QuadPart > LOG_MAX_BYTES)
            log_close_locked();
    }

    if (!g_logf.h) {
        if (!log_cache_open_locked(day, FALSE)) {
            g_logf.last_error = GetLastError();
            OutputDebugStringW(L"[UdiskTools] 主日志文件打开失败，改用备用文件 logs\\YYYYMMDD_alt.log；本行：\n");
            OutputDebugStringW(full);
            if (!log_cache_open_locked(day, TRUE)) {
                g_logf.last_error = GetLastError();
                OutputDebugStringW(L"[UdiskTools] 备用日志文件也打开失败，本行仅输出到调试器\n");
                OutputDebugStringW(full);
                LeaveCriticalSection(&g_logcs);
                return;
            }
        }
    }

    if (!log_write_locked(utf8, n)) {
        g_logf.last_error = GetLastError();
        OutputDebugStringW(L"[UdiskTools] 日志写入失败，重试备用文件；本行：\n");
        OutputDebugStringW(full);
        log_close_locked();
        if (!(log_cache_open_locked(day, TRUE) && log_write_locked(utf8, n))) {
            g_logf.last_error = GetLastError();
            log_close_locked();
            OutputDebugStringW(L"[UdiskTools] 备用日志写入失败，本行仅输出到调试器\n");
            OutputDebugStringW(full);
        }
    }
    LeaveCriticalSection(&g_logcs);
}

/* 复制 text 并用 PostMessage 投递；失败自行释放副本（不递归 applog） */
BOOL post_owned_string(HWND hwnd, UINT msg, WPARAM wparam, const wchar_t* text)
{
    wchar_t* copy;
    const wchar_t* src = text ? text : L"";

    if (!hwnd || !IsWindow(hwnd)) {
        OutputDebugStringW(L"[UdiskTools] post_owned_string：窗口无效，消息未投递：");
        OutputDebugStringW(src);
        OutputDebugStringW(L"\n");
        return FALSE;
    }
    copy = wcs_dup(src);
    if (!copy) {
        OutputDebugStringW(L"[UdiskTools] post_owned_string：内存不足，消息未投递\n");
        return FALSE;
    }
    if (!PostMessageW(hwnd, msg, wparam, (LPARAM)copy)) {
        DWORD e = GetLastError();
        free(copy);
        OutputDebugStringW(L"[UdiskTools] post_owned_string：PostMessage 失败，消息未投递：");
        OutputDebugStringW(src);
        OutputDebugStringW(L"\n");
        SetLastError(e);
        return FALSE;
    }
    return TRUE;    /* 接收方负责 free(lparam) */
}

void applog(int channel, LOGLEVEL lv, const wchar_t* fmt, ...)
{
    wchar_t line[2048];
    va_list ap;

    if (!fmt) fmt = L"";
    va_start(ap, fmt);
    _vsnwprintf(line, 2040, fmt, ap);
    va_end(ap);
    line[2040] = 0;

    filelog(lv, L"%s", line);   /* 文件日志失败不影响 GUI 侧投递 */

    if (g_hwndGui) {
        /* 消息参数：wparam=channel|level<<16, lparam=堆串（GUI 负责释放） */
        post_owned_string(g_hwndGui, WM_APP_LOG,
                          (WPARAM)((channel & 0xFFFF) | (((int)lv) << 16)), line);
    }
}

/* ================= 文本读取 ================= */

BOOL read_file_text(const wchar_t* path, wchar_t** out_text)
{
    HANDLE h;
    LARGE_INTEGER sz;
    BYTE* buf;
    DWORD size;
    DWORD total = 0;
    wchar_t* w = NULL;
    wchar_t err[256];
    TEXTENC enc = TEXT_ENC_UTF8;

    if (!out_text) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    *out_text = NULL;
    if (!path) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }

    h = CreateFileW(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;   /* 保留 CreateFile 的 GetLastError */
    if (!GetFileSizeEx(h, &sz)) {
        DWORD e = GetLastError();
        CloseHandle(h);
        SetLastError(e);
        return FALSE;
    }
    if (sz.QuadPart < 0 || sz.QuadPart > (64LL * 1024 * 1024)) {
        CloseHandle(h);
        filelog(LOG_WARN, L"[读取] 文件过大（%llu 字节 > 64MB），拒绝读取：%s",
                (unsigned long long)sz.QuadPart, path);
        SetLastError(ERROR_FILE_TOO_LARGE);   /* 放在日志之后：filelog 内部会改 LastError */
        return FALSE;
    }
    size = (DWORD)sz.QuadPart;
    buf = (BYTE*)malloc((size_t)size + 1);
    if (!buf) {
        CloseHandle(h);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    while (total < size) {
        DWORD rd = 0;
        if (!ReadFile(h, buf + total, size - total, &rd, NULL)) {
            DWORD e = GetLastError();
            CloseHandle(h);
            free(buf);
            filelog(LOG_WARN, L"[读取] ReadFile 失败（错误码 %lu）：%s",
                    (unsigned long)e, path);
            SetLastError(e);              /* ReadFile 失败保留系统错误码（日志后再设置） */
            return FALSE;
        }
        if (rd == 0) {                    /* 短读：文件被并发截断等 */
            CloseHandle(h);
            free(buf);
            filelog(LOG_WARN, L"[读取] 短读（%lu/%lu 字节）：%s",
                    (unsigned long)total, (unsigned long)size, path);
            SetLastError(ERROR_READ_FAULT);
            return FALSE;
        }
        total += rd;
    }
    buf[size] = 0;
    CloseHandle(h);

    if (!text_decode(buf, (size_t)size, &w, &enc, err, 256)) {
        free(buf);
        filelog(LOG_WARN, L"[读取] 文本编码解析失败（%s）：%s", path, err);
        SetLastError(ERROR_INVALID_DATA);     /* 放在日志之后（filelog 会改 LastError） */
        return FALSE;
    }
    free(buf);
    *out_text = w;
    return TRUE;
}

/* ================= MD5 文件摘要 ================= */

BOOL sha_or_md5_file(const wchar_t* path, wchar_t* md5hex_out, size_t outlen)
{
    /* 输出缓冲至少要放得下 32 位十六进制 + NUL */
    if (!path || !md5hex_out) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (outlen < 33) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;

    md5_context ctx;
    unsigned char digest[16];
    md5_init(&ctx);

    unsigned char buf[64 * 1024];
    for (;;) {
        DWORD rd = 0;
        if (!ReadFile(h, buf, (DWORD)sizeof(buf), &rd, NULL)) {
            /* 读错误：不得对已读前缀算哈希后返回 TRUE（会把截断文件当成完整文件） */
            DWORD err = GetLastError();
            CloseHandle(h);
            SetLastError(err);
            return FALSE;
        }
        if (rd == 0) break;             /* 只有真正的 EOF（读到 0 字节）才算成功 */
        md5_update(&ctx, buf, rd);
    }
    CloseHandle(h);
    md5_final(&ctx, digest);

    _snwprintf(md5hex_out, outlen,
               L"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
               digest[0], digest[1], digest[2], digest[3], digest[4], digest[5],
               digest[6], digest[7], digest[8], digest[9], digest[10],
               digest[11], digest[12], digest[13], digest[14], digest[15]);
    md5hex_out[outlen - 1] = 0;
    return TRUE;
}
