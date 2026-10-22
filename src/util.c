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
    if (!s) return NULL;
    if (len < 0) len = (int)strlen(s);
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, NULL, 0);
    if (n <= 0) {
        /* 非 UTF-8，回退 GBK（936） */
        n = MultiByteToWideChar(936, 0, s, len, NULL, 0);
        if (n <= 0) return NULL;
        wchar_t* w = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
        if (!w) return NULL;
        MultiByteToWideChar(936, 0, s, len, w, n);
        w[n] = 0;
        return w;
    }
    wchar_t* w = (wchar_t*)malloc((n + 1) * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, len, w, n);
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
static CRITICAL_SECTION g_logcs;
static BOOL g_cs_init = FALSE;

/* 自定义消息（与 gui.h 保持一致） */
#define WM_APP_LOG      (WM_APP + 1)
#define CH_COUNT        2

void log_set_gui_hwnd(HWND hwnd) { g_hwndGui = hwnd; }

static void log_cs_init(void)
{
    if (!g_cs_init) {
        InitializeCriticalSection(&g_logcs);
        g_cs_init = TRUE;
    }
}

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

/* 写文件日志：exe_dir\logs\YYYYMMDD.log（强制，不可关闭） */
void filelog(LOGLEVEL lv, const wchar_t* fmt, ...)
{
    log_cs_init();
    EnterCriticalSection(&g_logcs);
    {
        wchar_t date[16];
        __time64_t t = _time64(NULL);
        struct tm lt;
        _localtime64_s(&lt, &t);
        _snwprintf(date, 16, L"%04d%02d%02d.log", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
        date[15] = 0;

        wchar_t logdir[MAX_PATH];
        path_join(logdir, MAX_PATH, exe_dir(), L"logs");
        CreateDirectoryW(logdir, NULL);
        wchar_t path[MAX_PATH];
        path_join(path, MAX_PATH, logdir, date);

        HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            wchar_t line[2048];
            va_list ap;
            va_start(ap, fmt);
            _vsnwprintf(line, 2040, fmt, ap);
            va_end(ap);
            line[2040] = 0;

            wchar_t full[2300];
            _snwprintf(full, 2300, L"[%02d:%02d:%02d][%s] %s\r\n",
                       lt.tm_hour, lt.tm_min, lt.tm_sec, lv_tag(lv), line);
            char utf8[4600];
            int n = wide_to_utf8(full, utf8, 4600);
            if (n > 0) {
                DWORD written = 0;
                /* 若文件为空则补 UTF-8 BOM */
                LARGE_INTEGER sz;
                sz.QuadPart = 0;
                SetFilePointer(h, 0, NULL, FILE_END);
                GetFileSizeEx(h, &sz);
                if (sz.QuadPart == 0) {
                    static const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
                    WriteFile(h, bom, 3, &written, NULL);
                }
                WriteFile(h, utf8, (DWORD)n, &written, NULL);
            }
            CloseHandle(h);
        }
    }
    LeaveCriticalSection(&g_logcs);
}

void applog(int channel, LOGLEVEL lv, const wchar_t* fmt, ...)
{
    wchar_t line[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(line, 2040, fmt, ap);
    va_end(ap);
    line[2040] = 0;

    filelog(lv, L"%s", line);

    if (g_hwndGui && IsWindow(g_hwndGui)) {
        /* 消息参数：wparam=channel|level<<16, lparam=堆串（GUI 负责释放） */
        wchar_t* copy = wcs_dup(line);
        if (copy)
            PostMessageW(g_hwndGui, WM_APP_LOG,
                         (WPARAM)((channel & 0xFFFF) | (((int)lv) << 16)),
                         (LPARAM)copy);
    }
}

/* ================= 文本读取 ================= */

BOOL read_file_text(const wchar_t* path, wchar_t** out_text)
{
    *out_text = NULL;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (64LL * 1024 * 1024)) {
        CloseHandle(h);
        return FALSE;
    }
    DWORD size = (DWORD)sz.QuadPart;
    char* buf = (char*)malloc(size + 1);
    if (!buf) { CloseHandle(h); return FALSE; }
    DWORD rd = 0;
    ReadFile(h, buf, size, &rd, NULL);
    CloseHandle(h);
    buf[rd] = 0;

    wchar_t* w = NULL;
    if (rd >= 2 && (BYTE)buf[0] == 0xFF && (BYTE)buf[1] == 0xFE) {
        w = (wchar_t*)malloc(rd);               /* UTF-16 LE 直接复制 */
        if (w) memcpy(w, buf + 2, rd - 2), w[(rd - 2) / sizeof(wchar_t)] = 0;
    } else {
        const char* p = buf;
        int skip = 0;
        if (rd >= 3 && (BYTE)buf[0] == 0xEF && (BYTE)buf[1] == 0xBB && (BYTE)buf[2] == 0xBF) skip = 3;
        w = utf8_to_wide(p + skip, (int)(rd - skip));
        if (!w) w = ansi_to_wide(p + skip, (int)(rd - skip));
    }
    free(buf);
    *out_text = w;
    return w != NULL;
}

/* ================= MD5 文件摘要 ================= */

BOOL sha_or_md5_file(const wchar_t* path, wchar_t* md5hex_out, size_t outlen)
{
    md5_context ctx;
    unsigned char digest[16];
    md5_init(&ctx);

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    unsigned char buf[64 * 1024];
    DWORD rd = 0;
    while (ReadFile(h, buf, sizeof(buf), &rd, NULL) && rd > 0)
        md5_update(&ctx, buf, rd);
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
