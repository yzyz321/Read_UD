/* ============================================================
 * config.c - ini 配置解析实现
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>

#include "config.h"
#include "util.h"

/* ---------- 读整个 ini 文件并严格解码为宽文本 ---------- */

/* 失败原因写入 err（可为 NULL），并写 filelog/applog 具体原因 */
static void ini_err_set(wchar_t* err, size_t errlen, const wchar_t* fmt, ...)
{
    va_list ap;
    if (!err || errlen == 0) return;
    va_start(ap, fmt);
    _vsnwprintf(err, errlen - 1, fmt, ap);
    va_end(ap);
    err[errlen - 1] = 0;
}

/* 读取 ini 原始字节并用 util.c 的统一 text_decode 严格解码。
 * 成功返回堆宽串；失败返回 NULL（err 写具体原因：打开/过大/短读/编码）。
 * 共享模式包含 WRITE|DELETE，避免配置窗口/日志句柄并存时误报读取失败。 */
static wchar_t* read_ini_text(const wchar_t* path, wchar_t* err, size_t errlen)
{
    HANDLE h;
    LARGE_INTEGER sz;
    BYTE* buf;
    DWORD size;
    DWORD total = 0;
    wchar_t* w = NULL;
    TEXTENC enc = TEXT_ENC_UTF8;

    if (err && errlen) err[0] = 0;
    if (!path) {
        ini_err_set(err, errlen, L"ini 路径为空");
        return NULL;
    }

    h = CreateFileW(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        ini_err_set(err, errlen, L"打开 ini 失败（错误码 %lu）",
                    (unsigned long)GetLastError());
        return NULL;
    }
    if (!GetFileSizeEx(h, &sz)) {
        DWORD e = GetLastError();
        CloseHandle(h);
        ini_err_set(err, errlen, L"获取 ini 大小失败（错误码 %lu）", (unsigned long)e);
        return NULL;
    }
    if (sz.QuadPart < 0 || sz.QuadPart > (16LL * 1024 * 1024)) {
        CloseHandle(h);
        ini_err_set(err, errlen, L"ini 文件过大（%llu 字节 > 16MB）",
                    (unsigned long long)sz.QuadPart);
        return NULL;
    }
    size = (DWORD)sz.QuadPart;
    buf = (BYTE*)malloc((size_t)size + 1);
    if (!buf) {
        CloseHandle(h);
        ini_err_set(err, errlen, L"内存不足（需要 %lu 字节）", (unsigned long)size + 1);
        return NULL;
    }
    while (total < size) {
        DWORD rd = 0;
        if (!ReadFile(h, buf + total, size - total, &rd, NULL)) {
            DWORD e = GetLastError();
            CloseHandle(h);
            free(buf);
            ini_err_set(err, errlen,
                        L"读取 ini 失败（错误码 %lu，已读 %lu/%lu 字节）",
                        (unsigned long)e, (unsigned long)total, (unsigned long)size);
            return NULL;
        }
        if (rd == 0) {
            CloseHandle(h);
            free(buf);
            ini_err_set(err, errlen, L"读取 ini 短读（%lu/%lu 字节）",
                        (unsigned long)total, (unsigned long)size);
            return NULL;
        }
        total += rd;
    }
    buf[size] = 0;
    CloseHandle(h);

    {
        wchar_t derr[256];
        derr[0] = 0;
        if (!text_decode(buf, (size_t)size, &w, &enc, derr, 256)) {
            free(buf);
            ini_err_set(err, errlen, L"ini 编码识别/解码失败：%s",
                        derr[0] ? derr : L"未知编码");
            return NULL;
        }
    }
    free(buf);
    return w;
}

/* ============================================================
 * ini 行内注释判定（cfg_load 与 cfg_save_key 共用同一个 helper）
 *
 * READRULE=MUST_CONTAIN:8685;MIN_LEN:10 需要把 ';' 当作规则分隔符，
 * 而 "AUTO_COPY=1    ; 自动复制" 需要把 ';' 当作注释。统一规则：
 *   1) 行首（允许前导空格/制表符）的 ';' -> 注释起点（整行注释）；
 *   2) 前一个字符是空格或制表符的 ';'   -> 注释起点（行尾注释）；
 *   3) 其余 ';' 属于值内容（分号分隔的规则链、含 ';' 的路径等）。
 * 返回注释起点（';' 的下标）；没有注释返回 (size_t)-1。
 * ============================================================ */

static size_t ini_find_comment_start(const wchar_t* s)
{
    size_t i;
    BOOL only_ws = TRUE;    /* s[0..i) 是否全为行首空白 */

    if (!s) return (size_t)-1;
    for (i = 0; s[i]; i++) {
        if (s[i] == L';') {
            if (only_ws || (i > 0 && (s[i - 1] == L' ' || s[i - 1] == L'\t')))
                return i;
        }
        if (s[i] != L' ' && s[i] != L'\t') only_ws = FALSE;
    }
    return (size_t)-1;
}

static BOOL ini_has_linebreak(const wchar_t* s)
{
    if (!s) return FALSE;
    return wcschr(s, L'\r') != NULL || wcschr(s, L'\n') != NULL;
}

/* 值/键/节名首尾存在空白时无法与 cfg_load 的 trim 语义 round-trip */
static BOOL ini_has_outer_ws(const wchar_t* s)
{
    size_t n;
    if (!s) return FALSE;
    n = wcslen(s);
    if (n == 0) return FALSE;
    return s[0] == L' ' || s[0] == L'\t' ||
           s[n - 1] == L' ' || s[n - 1] == L'\t';
}

/* 写入后会被 cfg_load 当成行尾注释截断的值（如 "a ; b"），不允许
 * 保存，否则“写后回读 == 新值”必然不成立（返回假成功） */
static BOOL ini_value_roundtrips(const wchar_t* v)
{
    size_t i;
    if (!v || !v[0]) return TRUE;   /* 空值：不进入 v[1] 越界读 */
    for (i = 1; v[i]; i++) {
        if (v[i] == L';' && (v[i - 1] == L' ' || v[i - 1] == L'\t'))
            return FALSE;
    }
    return TRUE;
}

static void ini_trim_range(const wchar_t* s, const wchar_t* e,
                           const wchar_t** out_s, const wchar_t** out_e)
{
    while (s < e && (*s == L' ' || *s == L'\t' || *s == L'\r')) s++;
    while (e > s && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r')) e--;
    *out_s = s;
    *out_e = e;
}

/* 比较 [s,e) 与字面量（大小写不敏感） */
static BOOL ini_range_equals_ci(const wchar_t* s, const wchar_t* e, const wchar_t* lit)
{
    size_t n;
    if (!s || !e || !lit || e < s) return FALSE;
    n = (size_t)(e - s);
    if (n != wcslen(lit)) return FALSE;
    if (n == 0) return TRUE;
    return _wcsnicmp(s, lit, (int)n) == 0;
}

/* 若 content（已 NUL 结尾）是节头行，输出节名范围并返回 TRUE */
static BOOL ini_parse_header(const wchar_t* content,
                             const wchar_t** name_s, const wchar_t** name_e)
{
    size_t cpos = ini_find_comment_start(content);
    const wchar_t* s;
    const wchar_t* e;
    const wchar_t* rb;

    ini_trim_range(content, content + (cpos == (size_t)-1 ? wcslen(content) : cpos), &s, &e);
    if (s >= e || *s != L'[') return FALSE;
    rb = s + 1;
    while (rb < e && *rb != L']') rb++;
    if (rb >= e) return FALSE;
    ini_trim_range(s + 1, rb, name_s, name_e);
    return TRUE;
}

/* 探测文件的行尾风格（用于新增行的行尾；其余行逐行保留原行尾） */
static const wchar_t* ini_detect_eol(const wchar_t* text)
{
    const wchar_t* p;
    for (p = text; *p; p++) {
        if (*p == L'\r') return (p[1] == L'\n') ? L"\r\n" : L"\r";
        if (*p == L'\n') return L"\n";
    }
    return L"\r\n";
}

/* ---------- 可增长宽字符缓冲（保存时整文件重组，绝不截断） ---------- */

typedef struct {
    wchar_t* p;
    size_t   len;
    size_t   cap;
} WBUF;

static BOOL wbuf_reserve(WBUF* b, size_t extra)
{
    size_t need;
    size_t ncap;
    wchar_t* np;

    if (!b) return FALSE;
    if (extra > ((size_t)-1) - b->len - 1) return FALSE;
    need = b->len + extra + 1;
    if (need <= b->cap) return TRUE;
    ncap = b->cap ? b->cap : 256;
    while (ncap < need) {
        if (ncap > ((size_t)-1) / 2) { ncap = need; break; }
        ncap *= 2;
    }
    np = (wchar_t*)realloc(b->p, ncap * sizeof(wchar_t));
    if (!np) return FALSE;
    b->p = np;
    b->cap = ncap;
    return TRUE;
}

static BOOL wbuf_add_n(WBUF* b, const wchar_t* s, size_t n)
{
    if (!wbuf_reserve(b, n)) return FALSE;
    if (n) memcpy(b->p + b->len, s, n * sizeof(wchar_t));
    b->len += n;
    b->p[b->len] = 0;
    return TRUE;
}

static BOOL wbuf_add(WBUF* b, const wchar_t* s)
{
    return wbuf_add_n(b, s, wcslen(s));
}

/* 在指定位置前插入（用于“键不存在”时插到目标节节尾、尾部空行之前） */
static BOOL wbuf_insert_n(WBUF* b, size_t pos, const wchar_t* s, size_t n)
{
    if (!b || pos > b->len) return FALSE;
    if (!wbuf_reserve(b, n)) return FALSE;
    if (b->len > pos)
        memmove(b->p + pos + n, b->p + pos, (b->len - pos) * sizeof(wchar_t));
    if (n) memcpy(b->p + pos, s, n * sizeof(wchar_t));
    b->len += n;
    b->p[b->len] = 0;
    return TRUE;
}

/* ============================================================
 * 保存用：原样读取文件（识别并保留编码/BOM、属性、时间戳）
 * ============================================================ */

typedef enum {
    INI_ENC_UTF8 = 0,       /* UTF-8 无 BOM */
    INI_ENC_UTF8_BOM,       /* UTF-8 带 BOM */
    INI_ENC_UTF16LE,        /* UTF-16LE 带 BOM */
    INI_ENC_UTF16LE_NB,     /* UTF-16LE 无 BOM（零字节启发式识别） */
    INI_ENC_UTF16BE,        /* UTF-16BE 带 BOM */
    INI_ENC_UTF16BE_NB,     /* UTF-16BE 无 BOM（零字节启发式识别） */
    INI_ENC_ANSI            /* ANSI/GBK（与 cfg_load 的 CP936 回退一致） */
} INIENC;

typedef struct {
    INIENC   enc;
    wchar_t* text;
    DWORD    attrs;
    FILETIME ft_creation;
    FILETIME ft_access;
    FILETIME ft_write;
} INISRC;

static void ini_src_free(INISRC* src)
{
    if (!src) return;
    free(src->text);
    src->text = NULL;
}

/* 读原文件（严格校验）：返回解码后的宽文本 + 原编码/属性/时间戳。
 * 解码统一走 util.c 的 text_decode（UTF-8±BOM / UTF-16LE/BE±BOM / 无 BOM
 * UTF-16 启发式 / GBK；损坏 UTF-8 与二进制明确失败），保证保存时编码/BOM 不变。 */
static BOOL ini_read_source(const wchar_t* path, INISRC* src)
{
    HANDLE h;
    LARGE_INTEGER sz;
    BYTE* buf;
    DWORD size;
    DWORD got = 0;
    BOOL ok = FALSE;

    memset(src, 0, sizeof(*src));
    h = CreateFileW(path, GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > (16LL * 1024 * 1024)) {
        CloseHandle(h);
        return FALSE;
    }
    size = (DWORD)sz.QuadPart;
    buf = (BYTE*)malloc((size_t)size + 1);
    if (!buf) { CloseHandle(h); return FALSE; }
    /* 先取时间戳，再读数据：读取会更新访问时间，先读会把“原访问时间”污染成当前时间 */
    if (!GetFileTime(h, &src->ft_creation, &src->ft_access, &src->ft_write)) {
        free(buf);
        CloseHandle(h);
        return FALSE;
    }
    while (got < size) {
        DWORD rd = 0;
        if (!ReadFile(h, buf + got, size - got, &rd, NULL) || rd == 0) break;
        got += rd;
    }
    if (got != size) { free(buf); CloseHandle(h); return FALSE; }
    buf[size] = 0;
    CloseHandle(h);

    src->attrs = GetFileAttributesW(path);
    if (src->attrs == INVALID_FILE_ATTRIBUTES) { free(buf); return FALSE; }

    {
        wchar_t derr[256];
        TEXTENC enc = TEXT_ENC_UTF8;
        derr[0] = 0;
        if (!text_decode(buf, (size_t)size, &src->text, &enc, derr, 256)) {
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] 保存前读取 ini 失败（编码不可识别）：%s（%s）", path, derr);
            goto done;
        }
        switch (enc) {
        case TEXT_ENC_UTF8_BOM: src->enc = INI_ENC_UTF8_BOM; break;
        case TEXT_ENC_UTF16LE:
            /* 分清有无 BOM，保存时不得静默增删 BOM */
            src->enc = (size >= 2 && buf[0] == 0xFF && buf[1] == 0xFE)
                       ? INI_ENC_UTF16LE : INI_ENC_UTF16LE_NB;
            break;
        case TEXT_ENC_UTF16BE:
            src->enc = (size >= 2 && buf[0] == 0xFE && buf[1] == 0xFF)
                       ? INI_ENC_UTF16BE : INI_ENC_UTF16BE_NB;
            break;
        case TEXT_ENC_ANSI:     src->enc = INI_ENC_ANSI;     break;
        case TEXT_ENC_UTF8:
        default:                src->enc = INI_ENC_UTF8;     break;
        }
        ok = TRUE;
    }

done:
    free(buf);
    if (!ok) ini_src_free(src);
    return ok;
}

/* 按原编码/BOM 把重组后的宽文本编码回字节（buf 由调用方 free）
 * 注意：CP936 不支持 WC_ERR_INVALID_CHARS（实测返回 ERROR_INVALID_FLAGS），
 * 因此 ANSI 用 lpUsedDefaultChar 检测“无法映射的字符”，有则整体失败，
 * 绝不静默写成 '?'（避免损坏原文件）。 */
static BYTE* ini_encode_source(INIENC enc, const wchar_t* text, DWORD* outlen)
{
    int need;
    size_t bytes;
    size_t bom = 0;
    BYTE* out;
    UINT cp;
    DWORD flags;
    BOOL used_default = FALSE;
    int wrote;

    *outlen = 0;
    if (enc == INI_ENC_UTF16LE || enc == INI_ENC_UTF16LE_NB ||
        enc == INI_ENC_UTF16BE || enc == INI_ENC_UTF16BE_NB) {
        BOOL    be = (enc == INI_ENC_UTF16BE || enc == INI_ENC_UTF16BE_NB);
        BOOL    with_bom = (enc == INI_ENC_UTF16LE || enc == INI_ENC_UTF16BE);
        size_t  nch = wcslen(text);
        size_t  bombytes = with_bom ? 2 : 0;
        size_t  total = bombytes + nch * sizeof(wchar_t);
        size_t  i;
        if (total > 0xFFFFFFFFu) return NULL;
        out = (BYTE*)malloc(total ? total : 1);
        if (!out) return NULL;
        if (with_bom) {
            out[0] = be ? 0xFE : 0xFF;
            out[1] = be ? 0xFF : 0xFE;
        }
        for (i = 0; i < nch; i++) {
            unsigned int u = (unsigned int)text[i];
            BYTE* p = out + bombytes + i * 2;
            if (be) { p[0] = (BYTE)((u >> 8) & 0xFF); p[1] = (BYTE)(u & 0xFF); }
            else    { p[0] = (BYTE)(u & 0xFF);        p[1] = (BYTE)((u >> 8) & 0xFF); }
        }
        *outlen = (DWORD)total;
        return out;
    }
    if (enc == INI_ENC_UTF8_BOM) bom = 3;
    if (enc == INI_ENC_ANSI) {
        cp = 936u;
        flags = 0;
    } else {
        cp = CP_UTF8;
        flags = WC_ERR_INVALID_CHARS;
    }
    need = WideCharToMultiByte(cp, flags, text, -1, NULL, 0, NULL, NULL);
    if (need <= 0) return NULL;
    bytes = (size_t)need + bom;
    if (bytes > 0xFFFFFFFFu) return NULL;
    out = (BYTE*)malloc(bytes);
    if (!out) return NULL;
    if (bom) {
        out[0] = 0xEF;
        out[1] = 0xBB;
        out[2] = 0xBF;
    }
    wrote = WideCharToMultiByte(cp, flags, text, -1, (char*)out + bom, need,
                                NULL, &used_default);
    if (wrote != need || used_default) {
        free(out);
        return NULL;
    }
    *outlen = (DWORD)(bytes - 1);   /* 不写结尾 NUL */
    return out;
}

/* ---------- 键值存取（API 契约：NULL/0 长度安全） ---------- */

static void model_set_key(CFGMODEL* m, const wchar_t* key, const wchar_t* val)
{
    for (int i = 0; i < m->nkeys; i++) {
        if (_wcsicmp(m->keys[i].key, key) == 0) {
            lstrcpynW(m->keys[i].val, val, CFG_VAL_LEN);
            return;
        }
    }
    if (m->nkeys < CFG_MAX_KEYS) {
        lstrcpynW(m->keys[m->nkeys].key, key, CFG_KEY_LEN);
        lstrcpynW(m->keys[m->nkeys].val, val, CFG_VAL_LEN);
        m->nkeys++;
    }
}

const wchar_t* cfg_root_get(const CONFIG* c, const wchar_t* key)
{
    (void)c; (void)key;
    return NULL; /* 占位：TOOLSINFO 键由 cfg_load 直接装入 CONFIG 字段 */
}

const wchar_t* cfg_get(const CFGMODEL* m, const wchar_t* key)
{
    if (!m || !key) return NULL;
    for (int i = 0; i < m->nkeys; i++)
        if (_wcsicmp(m->keys[i].key, key) == 0) return m->keys[i].val;
    return NULL;
}

void cfg_get_def(const CFGMODEL* m, const wchar_t* key,
                 const wchar_t* def, wchar_t* out, size_t outlen)
{
    const wchar_t* v;
    const wchar_t* src;
    size_t n;

    if (!out || outlen == 0) return;
    v = cfg_get(m, key);
    src = (v && v[0]) ? v : def;
    if (!src) src = L"";
    n = wcslen(src);
    if (n >= outlen) n = outlen - 1;     /* 全程 size_t，不截断到 int */
    if (n) memcpy(out, src, n * sizeof(wchar_t));
    out[n] = 0;
}

int cfg_get_int(const CFGMODEL* m, const wchar_t* key, int def)
{
    const wchar_t* v;
    if (!m || !key) return def;
    v = cfg_get(m, key);
    return (v && v[0]) ? _wtoi(v) : def;
}

BOOL cfg_enabled(const CFGMODEL* m, const wchar_t* key)
{
    if (!m || !key) return FALSE;          /* 防御：无配置对象视为未启用 */
    return cfg_get_int(m, key, 1) != 0;
}

CFGMODEL* cfg_find_internal(CONFIG* c, const wchar_t* internal)
{
    if (!c || !internal) return NULL;
    for (int i = 0; i < c->nmodels; i++)
        if (_wcsicmp(c->models[i].internal, internal) == 0)
            return &c->models[i];
    return NULL;
}

CFGMODEL* cfg_find_display(CONFIG* c, const wchar_t* display)
{
    if (!c || !display) return NULL;
    for (int i = 0; i < c->nmodels; i++)
        if (_wcsicmp(c->models[i].display, display) == 0)   /* 与内部键匹配一致，大小写不敏感 */
            return &c->models[i];
    return NULL;
}

/* ---------- 解析期写入 + 告警（截断/容量超限不再静默，P2-13） ---------- */

static void cfg_check_key_len(const wchar_t* what, const wchar_t* k,
                              const wchar_t* v, int lineno)
{
    size_t kl = wcslen(k);
    size_t vl = wcslen(v);
    if (kl >= CFG_KEY_LEN) {
        applog(CH_ENGINE, LOG_WARN,
               L"[CFG] 第 %d 行%s键名过长（%llu 字符 >= %d），已截断：%.32s...",
               lineno, what, (unsigned long long)kl, CFG_KEY_LEN, k);
    }
    if (vl >= CFG_VAL_LEN) {
        applog(CH_ENGINE, LOG_WARN,
               L"[CFG] 第 %d 行%s键 \"%.32s\" 的值过长（%llu 字符 >= %d），已截断",
               lineno, what, k, (unsigned long long)vl, CFG_VAL_LEN);
    }
}

static void cfg_model_set_key_checked(CFGMODEL* m, const wchar_t* k,
                                      const wchar_t* v, int lineno)
{
    BOOL existed;
    int before;

    cfg_check_key_len(L"机型节", k, v, lineno);
    existed = (cfg_get(m, k) != NULL);
    before = m->nkeys;
    model_set_key(m, k, v);
    if (!existed && m->nkeys == before && before >= CFG_MAX_KEYS) {
        applog(CH_ENGINE, LOG_WARN,
               L"[CFG] 第 %d 行每机型键数超过上限 %d，忽略键 \"%.32s\"",
               lineno, CFG_MAX_KEYS, k);
    }
}

/* TOOLSINFO 全局键写入（带截断告警）；未知全局键保持忽略（既有行为） */
static void cfg_root_set_key_checked(CONFIG* cfg, CFGMODEL* root,
                                     const wchar_t* k, const wchar_t* v, int lineno)
{
    size_t vl = wcslen(v);

    if (_wcsicmp(k, L"TITLE") == 0) {
        if (vl >= CFG_VAL_LEN)
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] 第 %d 行 TITLE 值过长（%llu 字符 >= %d），已截断",
                   lineno, (unsigned long long)vl, CFG_VAL_LEN);
        lstrcpynW(cfg->title, v, CFG_VAL_LEN);
    } else if (_wcsicmp(k, L"VERSION") == 0) {
        if (vl >= CFG_VAL_LEN)
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] 第 %d 行 VERSION 值过长（%llu 字符 >= %d），已截断",
                   lineno, (unsigned long long)vl, CFG_VAL_LEN);
        lstrcpynW(cfg->version, v, CFG_VAL_LEN);
    } else if (_wcsicmp(k, L"ADMINPASS") == 0) {
        if (vl >= CFG_VAL_LEN)
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] 第 %d 行 ADMINPASS 值过长（%llu 字符 >= %d），已截断",
                   lineno, (unsigned long long)vl, CFG_VAL_LEN);
        lstrcpynW(cfg->adminpass, v, CFG_VAL_LEN);
    } else if (_wcsicmp(k, L"DEFAULTMODEL") == 0) {
        if (vl >= CFG_KEY_LEN)
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] 第 %d 行 DEFAULTMODEL 值过长（%llu 字符 >= %d），已截断",
                   lineno, (unsigned long long)vl, CFG_KEY_LEN);
        lstrcpynW(cfg->defaultmodel, v, CFG_KEY_LEN);
    } else if (_wcsicmp(k, L"TOPMOST") == 0) {
        /* 窗口置顶初始状态：仅接受 0/1，其它值按缺省 1 处理并告警 */
        if (_wcsicmp(v, L"0") == 0) {
            cfg->topmost = 0;
        } else if (_wcsicmp(v, L"1") == 0) {
            cfg->topmost = 1;
        } else {
            cfg->topmost = 1;
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] 第 %d 行 TOPMOST 值非法（%s，仅接受 0/1），按缺省 1 处理",
                   lineno, v);
        }
    } else if (_wcsnicmp(k, L"PRJNAME", 7) == 0 ||
               _wcsnicmp(k, L"DISPLAYNAME", 11) == 0) {
        cfg_model_set_key_checked(root, k, v, lineno);
    }
    /* 其它全局键（如 PRJNUM）：保持既有“忽略”行为，不告警 */
}

/* ---------- 主解析 ---------- */

BOOL cfg_load(const wchar_t* ini_path, CONFIG* cfg)
{
    wchar_t errbuf[512];
    wchar_t* text;
    static CFGMODEL root;   /* TOOLSINFO 键暂存：仅解析期使用 */
    CFGMODEL* cur;
    enum { SEC_ROOT, SEC_MODEL, SEC_SKIP } sec_kind = SEC_ROOT;
    int lineno = 0;
    BOOL toolsinfo_seen = FALSE;

    if (!cfg) {
        SetLastError(ERROR_INVALID_PARAMETER);
        applog(CH_ENGINE, LOG_ERR, L"[CFG] cfg_load：CONFIG 输出指针为空");
        return FALSE;
    }
    memset(cfg, 0, sizeof(CONFIG));
    cfg->topmost = 1;   /* TOPMOST 缺省：置顶开启（未写键时 checkbox 默认勾选） */
    if (!ini_path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        applog(CH_ENGINE, LOG_ERR, L"[CFG] cfg_load：ini 路径为空");
        return FALSE;
    }

    errbuf[0] = 0;
    text = read_ini_text(ini_path, errbuf, 512);
    if (!text) {
        applog(CH_ENGINE, LOG_ERR, L"[CFG] 加载失败：无法读取/解码 ini：%s（%s）",
               ini_path, errbuf[0] ? errbuf : L"未知原因");
        return FALSE;
    }

    memset(&root, 0, sizeof(root));
    cur = &root;
    {
        wchar_t* cursor = text;
        while (cursor && *cursor) {
            wchar_t* line = cursor;
            wchar_t* nl = wcschr(cursor, L'\n');
            size_t cpos;
            wchar_t* s;
            wchar_t* eq;

            if (nl) { *nl = 0; cursor = nl + 1; }
            else { cursor = NULL; }
            lineno++;

            /* 行内注释：只有行首/空白后的 ';' 才是注释（与 cfg_save_key 共用 helper） */
            cpos = ini_find_comment_start(line);
            if (cpos != (size_t)-1) line[cpos] = 0;
            s = wcs_trim(line);
            if (!s[0]) continue;

            if (s[0] == L'[') {
                wchar_t* end = wcschr(s, L']');
                wchar_t* name;
                size_t nlen;

                if (!end) {   /* P2-13-2 畸形节头：忽略并暂停当前节 */
                    applog(CH_ENGINE, LOG_WARN,
                           L"[CFG] 第 %d 行节头缺少 ']'，已忽略该行及其后键直到下一个合法节头：%.48s",
                           lineno, s);
                    cur = NULL;
                    sec_kind = SEC_SKIP;
                    continue;
                }
                *end = 0;
                name = wcs_trim(s + 1);
                nlen = wcslen(name);
                if (_wcsicmp(name, L"TOOLSINFO") == 0) {
                    if (toolsinfo_seen) {
                        applog(CH_ENGINE, LOG_WARN,
                               L"[CFG] 第 %d 行重复 [TOOLSINFO] 节，后续键继续并入全局节",
                               lineno);
                    }
                    toolsinfo_seen = TRUE;
                    cur = &root;
                    sec_kind = SEC_ROOT;
                } else if (nlen == 0) {   /* P2-13-3 空节名：忽略 */
                    applog(CH_ENGINE, LOG_WARN,
                           L"[CFG] 第 %d 行节名为空（[]），已忽略该节及其后键", lineno);
                    cur = NULL;
                    sec_kind = SEC_SKIP;
                } else if (nlen >= CFG_KEY_LEN) {   /* P2-13-5 超长节名：忽略 */
                    applog(CH_ENGINE, LOG_WARN,
                           L"[CFG] 第 %d 行节名过长（%llu 字符 >= %d），已忽略该节及其后键",
                           lineno, (unsigned long long)nlen, CFG_KEY_LEN);
                    cur = NULL;
                    sec_kind = SEC_SKIP;
                } else {
                    CFGMODEL* dup = cfg_find_internal(cfg, name);
                    if (dup) {   /* P2-13-4 重复节：合并到已有同名机型 */
                        applog(CH_ENGINE, LOG_WARN,
                               L"[CFG] 第 %d 行节名 [%s] 重复，合并到已有同名机型（后出现的键覆盖）",
                               lineno, name);
                        cur = dup;
                        sec_kind = SEC_MODEL;
                    } else if (cfg->nmodels >= CFG_MAX_MODELS) {   /* P2-13-1 机型容量 */
                        applog(CH_ENGINE, LOG_WARN,
                               L"[CFG] 第 %d 行机型数超过上限 %d，忽略节 [%s] 及其后键",
                               lineno, CFG_MAX_MODELS, name);
                        cur = NULL;
                        sec_kind = SEC_SKIP;
                    } else {
                        cur = &cfg->models[cfg->nmodels++];
                        memset(cur, 0, sizeof(CFGMODEL));
                        lstrcpynW(cur->internal, name, CFG_KEY_LEN);
                        sec_kind = SEC_MODEL;
                    }
                }
                continue;
            }

            /* ---- 非节头行 ---- */
            eq = wcschr(s, L'=');
            if (!cur) {
                if (sec_kind == SEC_SKIP) {   /* 当前节已被忽略：逐行告警 */
                    if (eq) {
                        *eq = 0;
                        applog(CH_ENGINE, LOG_WARN,
                               L"[CFG] 第 %d 行当前节无效/已忽略，键 \"%.48s\" 被忽略",
                               lineno, wcs_trim(s));
                    } else {
                        applog(CH_ENGINE, LOG_WARN,
                               L"[CFG] 第 %d 行当前节无效/已忽略，行内容被忽略", lineno);
                    }
                }
                continue;
            }
            if (!eq) {
                applog(CH_ENGINE, LOG_WARN,
                       L"[CFG] 第 %d 行不是合法的 \"键=值\" 行，已忽略", lineno);
                continue;
            }
            *eq = 0;
            {
                wchar_t* k = wcs_trim(s);
                wchar_t* v = wcs_trim(eq + 1);
                if (!k[0]) {
                    applog(CH_ENGINE, LOG_WARN,
                           L"[CFG] 第 %d 行键名为空，已忽略", lineno);
                    continue;
                }
                if (cur == &root)
                    cfg_root_set_key_checked(cfg, &root, k, v, lineno);
                else
                    cfg_model_set_key_checked(cur, k, v, lineno);
            }
        }
    }
    free(text);

    /* 组装双层映射：PRJNAMEn=内部键 → 节名匹配；DISPLAYNAMEn=显示名 */
    for (int n = 1; n <= CFG_MAX_MODELS; n++) {
        wchar_t kb[32];
        wchar_t db[32];
        const wchar_t* internal;
        const wchar_t* disp;
        CFGMODEL* m;

        _snwprintf(kb, 32, L"PRJNAME%d", n);
        kb[31] = 0;
        internal = cfg_get(&root, kb);
        if (!internal || !internal[0]) continue;
        if (wcslen(internal) >= CFG_KEY_LEN) {
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] PRJNAME%d 的内部键过长（%llu 字符 >= %d），无法匹配机型节，已忽略",
                   n, (unsigned long long)wcslen(internal), CFG_KEY_LEN);
            continue;
        }
        m = cfg_find_internal(cfg, internal);
        if (!m) {   /* PRJNAME 与节名不匹配/节名被忽略 */
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] PRJNAME%d=%s 未匹配到任何机型节（该映射已忽略）", n, internal);
            continue;
        }
        _snwprintf(db, 32, L"DISPLAYNAME%d", n);
        db[31] = 0;
        disp = cfg_get(&root, db);
        lstrcpynW(m->display, (disp && disp[0]) ? disp : internal, CFG_VAL_LEN);
    }

    /* 显示名缺失回退内部键 */
    for (int i = 0; i < cfg->nmodels; i++)
        if (!cfg->models[i].display[0])
            lstrcpynW(cfg->models[i].display, cfg->models[i].internal, CFG_VAL_LEN);

    /* 默认值兜底 */
    if (!cfg->title[0])    lstrcpynW(cfg->title, L"UdiskTools", CFG_VAL_LEN);
    if (!cfg->version[0])  lstrcpynW(cfg->version, L"1.0.0", CFG_VAL_LEN);
    if (cfg->nmodels > 0) {
        if (!cfg->defaultmodel[0]) {
            lstrcpynW(cfg->defaultmodel, cfg->models[0].internal, CFG_KEY_LEN);
        } else if (!cfg_find_internal(cfg, cfg->defaultmodel)) {
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] DEFAULTMODEL=%s 未匹配到任何机型节，已回退为 %s",
                   cfg->defaultmodel, cfg->models[0].internal);
            lstrcpynW(cfg->defaultmodel, cfg->models[0].internal, CFG_KEY_LEN);
        }
    }

    if (cfg->nmodels <= 0) {
        applog(CH_ENGINE, LOG_ERR,
               L"[CFG] 加载失败：%s 中没有有效机型节（缺少 [机型] 节/节名被忽略/超出机型上限）",
               ini_path);
        return FALSE;
    }
    return TRUE;
}

/* ============================================================
 * 单键写回（重写版）
 *
 * 契约（与 config.h 注释一致）：
 *   - 只支持机型节（GUI 的唯一调用方式）；节不存在返回 FALSE；
 *   - 键存在 -> 就地替换该行：保留原 key 拼写、前导空白、'=' 前空白、
 *     行内注释后缀（含注释前空白）与原始行尾；值中允许 ';'；
 *   - 键不存在 -> 插入目标节节尾（下一个节头之前），不会落到最后一个节；
 *   - 保留原编码/BOM（UTF-8 有/无 BOM、UTF-16LE/BE 有/无 BOM、ANSI/GBK）；
 *   - 行尾逐行保留（先剥离解析用的 '\r'，再按原风格重建），连续保存
 *     字节稳定（不再出现 \r 累积、文件膨胀）；
 *   - 输出用动态缓冲，任何行/值都不截断；放不下/无法回读一致则 FALSE；
 *   - 安全写：唯一 tmp（含 PID）-> 校验 WriteFile/字节数 -> FlushFileBuffers
 *     -> MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)；失败删除 tmp、原文件不变；
 *   - 只读文件直接 FALSE（不创建 tmp、不改原文件）；
 *   - 保存/恢复原文件属性与创建/访问/修改时间；
 *   - 写后重新 cfg_load，校验目标键 == 新值且该机型键数符合预期，否则 FALSE。
 * ============================================================ */

BOOL cfg_save_key(const wchar_t* ini_path, const wchar_t* section,
                  const wchar_t* key, const wchar_t* newval)
{
    INISRC src;
    WBUF out;
    const wchar_t* eol_style;
    size_t pos;
    size_t tlen;
    size_t insert_at = 0;      /* 目标节最后一个非空行的输出末尾（新键插入点） */
    BOOL in_sec = FALSE;
    BOOL sec_seen = FALSE;
    BOOL key_seen = FALSE;
    BOOL inserted = FALSE;
    BOOL eof_had_eol;
    BOOL ok = FALSE;
    wchar_t* tmppath = NULL;
    BYTE* bytes = NULL;
    DWORD nbytes = 0;
    CONFIG* before_cfg = NULL;
    CONFIG* after_cfg = NULL;
    int model_keys_before = -1;
    BOOL loader_key_seen = FALSE;

    /* ---------- 参数校验（拒绝会破坏 ini 结构/无法回读的内容） ---------- */
    if (!ini_path || !section || !key || !newval) return FALSE;
    if (!section[0] || !key[0]) return FALSE;
    if (ini_has_linebreak(section) || ini_has_linebreak(key) ||
        ini_has_linebreak(newval)) return FALSE;
    if (wcslen(section) >= CFG_KEY_LEN || wcslen(key) >= CFG_KEY_LEN) return FALSE;
    /* 超过配置模型容量（CFG_VAL_LEN-1）时 cfg_load 会截断，拒绝而非截断 */
    if (wcslen(newval) >= CFG_VAL_LEN) return FALSE;
    if (wcschr(key, L'=') || wcschr(key, L'[') || wcschr(key, L']')) return FALSE;
    if (ini_has_outer_ws(section) || ini_has_outer_ws(key) ||
        ini_has_outer_ws(newval)) return FALSE;
    if (!ini_value_roundtrips(newval)) return FALSE;

    memset(&out, 0, sizeof(out));
    memset(&src, 0, sizeof(src));

    /* 只读/目录：直接失败，不读不写、原文件保持不变（也避免读操作影响访问时间） */
    {
        DWORD pre_attrs = GetFileAttributesW(ini_path);
        if (pre_attrs == INVALID_FILE_ATTRIBUTES ||
            (pre_attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)))
            return FALSE;
    }

    /* ---------- 读原文件（保留编码/属性/时间戳） ---------- */
    if (!ini_read_source(ini_path, &src)) return FALSE;
    if (!src.text) {              /* 防御：读取成功但文本为空指针（不应发生） */
        ini_src_free(&src);
        return FALSE;
    }
    if (src.attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) {
        ini_src_free(&src);
        return FALSE;   /* 只读文件：直接失败，原文件保持不变 */
    }

    eol_style = ini_detect_eol(src.text);
    tlen = wcslen(src.text);
    eof_had_eol = (tlen > 0 &&
                   (src.text[tlen - 1] == L'\n' || src.text[tlen - 1] == L'\r'));
    pos = 0;

    /* ---------- 逐行扫描、重组（不截断；其余行逐字节等价） ---------- */
    while (pos < tlen) {
        size_t start;
        size_t nl;
        size_t end;
        const wchar_t* eol;
        wchar_t saved;
        wchar_t* content;

        start = pos;
        nl = start;
        while (nl < tlen && src.text[nl] != L'\n') nl++;
        end = nl;
        if (nl < tlen) {
            eol = L"\n";
            if (end > start && src.text[end - 1] == L'\r') { end--; eol = L"\r\n"; }
            pos = nl + 1;
        } else {
            eol = L"";
            if (end > start && src.text[end - 1] == L'\r') { end--; eol = L"\r"; }
            pos = tlen;
        }
        saved = src.text[end];
        src.text[end] = 0;
        content = src.text + start;

        {
            const wchar_t* hs = NULL;
            const wchar_t* he = NULL;

            if (ini_parse_header(content, &hs, &he)) {
                /* 目标节结束：键未找到则插在该节节尾（尾部空行/下一节头之前） */
                if (in_sec && !key_seen && !inserted) {
                    WBUF kv;
                    memset(&kv, 0, sizeof(kv));
                    if (!wbuf_add(&kv, key) ||
                        !wbuf_add_n(&kv, L"=", 1) ||
                        !wbuf_add(&kv, newval) ||
                        !wbuf_add(&kv, eol_style) ||
                        !wbuf_insert_n(&out, insert_at, kv.p, kv.len)) {
                        free(kv.p);
                        goto cleanup;
                    }
                    free(kv.p);
                    inserted = TRUE;
                }
                in_sec = FALSE;
                if (!sec_seen && ini_range_equals_ci(hs, he, section)) {
                    sec_seen = TRUE;
                    in_sec = TRUE;
                }
                if (!wbuf_add_n(&out, content, end - start) ||
                    !wbuf_add(&out, eol)) goto cleanup;
                if (in_sec) insert_at = out.len;    /* 节头本身作为插入点兜底 */
            } else if (in_sec) {
                size_t cpos = ini_find_comment_start(content);
                const wchar_t* body_e = (cpos == (size_t)-1)
                                        ? content + wcslen(content)
                                        : content + cpos;
                const wchar_t* ks = content;
                const wchar_t* ke = body_e;
                const wchar_t* eq = NULL;
                const wchar_t* p;

                ini_trim_range(content, body_e, &ks, &ke);
                for (p = ks; p < ke; p++) {
                    if (*p == L'=') { eq = p; break; }
                }
                if (eq) {
                    const wchar_t* kend = eq;
                    while (kend > ks && (kend[-1] == L' ' || kend[-1] == L'\t')) kend--;
                    if (ini_range_equals_ci(ks, kend, key)) {
                        /* 替换目标行：前导空白 + 原 key 拼写 + '=' + 新值 + 注释后缀 */
                        size_t body_len = wcslen(content);
                        size_t eqoff = (size_t)(eq - content);
                        size_t key_start = 0;
                        const wchar_t* suffix = content + body_len;

                        if (cpos != (size_t)-1) {
                            size_t ws = cpos;
                            while (ws > eqoff + 1 &&
                                   (content[ws - 1] == L' ' || content[ws - 1] == L'\t'))
                                ws--;
                            suffix = content + ws;
                        }
                        while (key_start < body_len &&
                               (content[key_start] == L' ' ||
                                content[key_start] == L'\t'))
                            key_start++;
                        if (!wbuf_add_n(&out, content, key_start) ||
                            !wbuf_add_n(&out, content + key_start, eqoff - key_start) ||
                            !wbuf_add_n(&out, L"=", 1) ||
                            !wbuf_add(&out, newval) ||
                            !wbuf_add(&out, suffix) ||
                            !wbuf_add(&out, eol)) goto cleanup;
                        key_seen = TRUE;
                        src.text[end] = saved;
                        continue;
                    }
                }
                if (!wbuf_add_n(&out, content, end - start) ||
                    !wbuf_add(&out, eol)) goto cleanup;
                if (ks < ke) insert_at = out.len;   /* 只跟踪非空/非纯注释行 */
            } else {
                if (!wbuf_add_n(&out, content, end - start) ||
                    !wbuf_add(&out, eol)) goto cleanup;
            }
        }
        src.text[end] = saved;
    }

    if (!sec_seen) goto cleanup;   /* 目标节不存在：失败，绝不追加到文件末尾 */

    /* 键不存在且目标节是最后一个节：插到节尾（尾部空行之前，保持末尾换行风格） */
    if (in_sec && !key_seen && !inserted) {
        WBUF kv;
        memset(&kv, 0, sizeof(kv));
        if (insert_at < out.len) {
            /* 节后还有尾部空行/注释：插在它们之前，新键紧贴节内容 */
            if (!wbuf_add(&kv, key) ||
                !wbuf_add_n(&kv, L"=", 1) ||
                !wbuf_add(&kv, newval) ||
                !wbuf_add(&kv, eol_style)) { free(kv.p); goto cleanup; }
        } else {
            if (out.len > 0 && out.p[out.len - 1] != L'\n' &&
                out.p[out.len - 1] != L'\r') {
                if (!wbuf_add(&kv, eol_style)) { free(kv.p); goto cleanup; }
            }
            if (!wbuf_add(&kv, key) ||
                !wbuf_add_n(&kv, L"=", 1) ||
                !wbuf_add(&kv, newval)) { free(kv.p); goto cleanup; }
            if (eof_had_eol && !wbuf_add(&kv, eol_style)) { free(kv.p); goto cleanup; }
        }
        if (!wbuf_insert_n(&out, insert_at, kv.p, kv.len)) {
            free(kv.p);
            goto cleanup;
        }
        free(kv.p);
        inserted = TRUE;
    }
    if (!key_seen && !inserted) goto cleanup;

    /* ---------- 写前基线：cfg_load 视角的目标机型键数 ---------- */
    before_cfg = (CONFIG*)malloc(sizeof(CONFIG));
    if (!before_cfg) goto cleanup;
    if (!cfg_load(ini_path, before_cfg)) goto cleanup;
    {
        CFGMODEL* bm = cfg_find_internal(before_cfg, section);
        if (!bm) goto cleanup;   /* 仅支持机型节（与 GUI 调用方式一致） */
        model_keys_before = bm->nkeys;
        loader_key_seen = (cfg_get(bm, key) != NULL);
        /* 模型容量/截断导致写后必然回读失败的情形，写前就返回 FALSE */
        if (!loader_key_seen && model_keys_before >= CFG_MAX_KEYS) goto cleanup;
        if (key_seen && !loader_key_seen) goto cleanup;
    }

    /* ---------- 编码回原格式并安全写出 ---------- */
    bytes = ini_encode_source(src.enc, out.p, &nbytes);
    if (!bytes || nbytes == 0) goto cleanup;

    {
        size_t plen = wcslen(ini_path) + 48;
        int wr;
        tmppath = (wchar_t*)malloc(plen * sizeof(wchar_t));
        if (!tmppath) goto cleanup;
        wr = _snwprintf(tmppath, plen, L"%s.tmp.%lu",
                        ini_path, (unsigned long)GetCurrentProcessId());
        if (wr < 0 || (size_t)wr >= plen) goto cleanup;
    }
    {
        HANDLE h = CreateFileW(tmppath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        BOOL wok = FALSE;
        if (h != INVALID_HANDLE_VALUE) {
            DWORD total = 0;
            wok = TRUE;
            while (total < nbytes) {
                DWORD chunk = nbytes - total;
                DWORD done = 0;
                if (!WriteFile(h, bytes + total, chunk, &done, NULL) || done == 0) {
                    wok = FALSE;
                    break;
                }
                total += done;
            }
            if (wok && total != nbytes) wok = FALSE;
            if (wok && !FlushFileBuffers(h)) wok = FALSE;
            if (!CloseHandle(h)) wok = FALSE;
        }
        if (!wok) { DeleteFileW(tmppath); goto cleanup; }
    }
    if (!MoveFileExW(tmppath, ini_path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmppath);
        goto cleanup;
    }

    /* ---------- 写后回读校验（cfg_load：目标键 == 新值、键数符合预期） ---------- */
    {
        BOOL verify_ok = FALSE;
        BOOL restore_ok = TRUE;

        after_cfg = (CONFIG*)malloc(sizeof(CONFIG));
        if (!after_cfg) goto cleanup;
        if (cfg_load(ini_path, after_cfg)) {
            CFGMODEL* am = cfg_find_internal(after_cfg, section);
            if (am) {
                const wchar_t* got = cfg_get(am, key);
                int expect = model_keys_before + (loader_key_seen ? 0 : 1);
                if (got && wcscmp(got, newval) == 0 && am->nkeys == expect)
                    verify_ok = TRUE;
            }
        }

        /* 恢复原文件属性与时间戳（放在回读之后，避免回读更新访问时间） */
        if (!SetFileAttributesW(ini_path, src.attrs)) {
            restore_ok = FALSE;
        } else {
            HANDLE ha = CreateFileW(ini_path, FILE_WRITE_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (ha == INVALID_HANDLE_VALUE) {
                restore_ok = FALSE;
            } else {
                if (!SetFileTime(ha, &src.ft_creation, &src.ft_access, &src.ft_write))
                    restore_ok = FALSE;
                CloseHandle(ha);
            }
        }
        ok = verify_ok && restore_ok;
    }

cleanup:
    if (tmppath) DeleteFileW(tmppath);   /* 已改名时不存在，安全无副作用 */
    free(bytes);
    free(tmppath);
    free(before_cfg);
    free(after_cfg);
    free(out.p);
    ini_src_free(&src);
    return ok;
}
