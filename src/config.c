/* ============================================================
 * config.c - ini 配置解析实现
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "util.h"

/* ---------- 读整个文件并转为 UTF-16 行数组 ---------- */

static wchar_t* read_ini_text(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (16LL * 1024 * 1024)) {
        CloseHandle(h);
        return NULL;
    }
    DWORD size = (DWORD)sz.QuadPart;
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    DWORD rd = 0;
    ReadFile(h, buf, size, &rd, NULL);
    CloseHandle(h);
    buf[rd] = 0;

    wchar_t* w = NULL;
    if (rd >= 2 && (BYTE)buf[0] == 0xFF && (BYTE)buf[1] == 0xFE) {
        size_t nch = (rd - 2) / sizeof(wchar_t);
        w = (wchar_t*)malloc((nch + 1) * sizeof(wchar_t));
        if (w) {
            memcpy(w, buf + 2, nch * sizeof(wchar_t));
            w[nch] = 0;
        }
    } else {
        const char* p = buf;
        int skip = 0;
        if (rd >= 3 && (BYTE)buf[0] == 0xEF && (BYTE)buf[1] == 0xBB && (BYTE)buf[2] == 0xBF) skip = 3;
        w = utf8_to_wide(p + skip, (int)(rd - skip));
        if (!w) w = ansi_to_wide(p + skip, (int)(rd - skip));
    }
    free(buf);
    return w;
}

/* ---------- 键值存取 ---------- */

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
    for (int i = 0; i < m->nkeys; i++)
        if (_wcsicmp(m->keys[i].key, key) == 0) return m->keys[i].val;
    return NULL;
}

void cfg_get_def(const CFGMODEL* m, const wchar_t* key,
                 const wchar_t* def, wchar_t* out, size_t outlen)
{
    const wchar_t* v = cfg_get(m, key);
    lstrcpynW(out, (v && v[0]) ? v : def, (int)outlen);
}

int cfg_get_int(const CFGMODEL* m, const wchar_t* key, int def)
{
    const wchar_t* v = cfg_get(m, key);
    return (v && v[0]) ? _wtoi(v) : def;
}

BOOL cfg_enabled(const CFGMODEL* m, const wchar_t* key)
{
    return cfg_get_int(m, key, 1) != 0;
}

CFGMODEL* cfg_find_internal(CONFIG* c, const wchar_t* internal)
{
    for (int i = 0; i < c->nmodels; i++)
        if (_wcsicmp(c->models[i].internal, internal) == 0)
            return &c->models[i];
    return NULL;
}

CFGMODEL* cfg_find_display(CONFIG* c, const wchar_t* display)
{
    for (int i = 0; i < c->nmodels; i++)
        if (wcscmp(c->models[i].display, display) == 0)
            return &c->models[i];
    return NULL;
}

/* ---------- 主解析 ---------- */

BOOL cfg_load(const wchar_t* ini_path, CONFIG* cfg)
{
    memset(cfg, 0, sizeof(CONFIG));
    wchar_t* text = read_ini_text(ini_path);
    if (!text) return FALSE;

    /* TOOLSINFO 键暂存于根层：借用一个未暴露的缓冲 */
    static CFGMODEL root;   /* 仅解析期使用 */
    memset(&root, 0, sizeof(root));

    wchar_t* ctx = NULL;
    wchar_t* line = wcstok_s(text, L"\n", &ctx);
    CFGMODEL* cur = &root;

    while (line) {
        wchar_t* hash = wcschr(line, L';');   /* 行内注释（';' 之后丢弃） */
        if (hash) *hash = 0;
        wchar_t* s = wcs_trim(line);

        if (s[0] == L'[') {
            wchar_t* end = wcschr(s, L']');
            if (end) {
                *end = 0;
                wchar_t* name = wcs_trim(s + 1);
                if (_wcsicmp(name, L"TOOLSINFO") == 0) {
                    cur = &root;
                } else if (cfg->nmodels < CFG_MAX_MODELS) {
                    cur = &cfg->models[cfg->nmodels++];
                    memset(cur, 0, sizeof(CFGMODEL));
                    lstrcpynW(cur->internal, name, CFG_KEY_LEN);
                } else {
                    cur = NULL;
                }
            }
        } else if (cur && s[0]) {
            wchar_t* eq = wcschr(s, L'=');
            if (eq) {
                *eq = 0;
                wchar_t* k = wcs_trim(s);
                wchar_t* v = wcs_trim(eq + 1);
                if (cur == &root) {
                    /* TOOLSINFO 全局键 */
                    if (_wcsicmp(k, L"TITLE") == 0)
                        lstrcpynW(cfg->title, v, CFG_VAL_LEN);
                    else if (_wcsicmp(k, L"VERSION") == 0)
                        lstrcpynW(cfg->version, v, CFG_VAL_LEN);
                    else if (_wcsicmp(k, L"ADMINPASS") == 0)
                        lstrcpynW(cfg->adminpass, v, CFG_VAL_LEN);
                    else if (_wcsicmp(k, L"DEFAULTMODEL") == 0)
                        lstrcpynW(cfg->defaultmodel, v, CFG_KEY_LEN);
                    else if (_wcsnicmp(k, L"PRJNAME", 7) == 0)
                        model_set_key(&root, k, v);
                    else if (_wcsnicmp(k, L"DISPLAYNAME", 11) == 0)
                        model_set_key(&root, k, v);
                } else {
                    model_set_key(cur, k, v);
                }
            }
        }
        line = wcstok_s(NULL, L"\n", &ctx);
    }
    free(text);

    /* 组装双层映射：PRJNAMEn=内部键 → 节名匹配；DISPLAYNAMEn=显示名 */
    wchar_t kb[32];
    for (int n = 1; n <= CFG_MAX_MODELS; n++) {
        _snwprintf(kb, 32, L"PRJNAME%d", n);
        const wchar_t* internal = cfg_get(&root, kb);
        if (!internal || !internal[0]) continue;
        CFGMODEL* m = cfg_find_internal(cfg, internal);
        if (!m) continue;   /* 节缺失则忽略该机型 */
        wchar_t db[32];
        _snwprintf(db, 32, L"DISPLAYNAME%d", n);
        const wchar_t* disp = cfg_get(&root, db);
        lstrcpynW(m->display, (disp && disp[0]) ? disp : internal, CFG_VAL_LEN);
    }

    /* 显示名缺失回退内部键 */
    for (int i = 0; i < cfg->nmodels; i++)
        if (!cfg->models[i].display[0])
            lstrcpynW(cfg->models[i].display, cfg->models[i].internal, CFG_VAL_LEN);

    /* 默认值兜底 */
    if (!cfg->title[0])    lstrcpynW(cfg->title, L"UdiskTools", CFG_VAL_LEN);
    if (!cfg->version[0])  lstrcpynW(cfg->version, L"1.0.0", CFG_VAL_LEN);
    if (cfg->nmodels > 0 && (!cfg->defaultmodel[0] ||
        !cfg_find_internal(cfg, cfg->defaultmodel)))
        lstrcpynW(cfg->defaultmodel, cfg->models[0].internal, CFG_KEY_LEN);

    return cfg->nmodels > 0;
}

/* ---------- 单键写回（保留其余行与行内注释） ---------- */

BOOL cfg_save_key(const wchar_t* ini_path, const wchar_t* section,
                  const wchar_t* key, const wchar_t* newval)
{
    wchar_t* text = read_ini_text(ini_path);
    if (!text) return FALSE;

    /* 逐行扫描：目标节内的 KEY= 行替换值（保留行内 '; ' 注释） */
    wchar_t* out = (wchar_t*)malloc((wcslen(text) + 1024) * sizeof(wchar_t));
    if (!out) { free(text); return FALSE; }
    out[0] = 0;
    size_t olen = 0;
    BOOL in_sec = FALSE, replaced = FALSE;

    wchar_t* ctx = NULL;
    wchar_t* line = wcstok_s(text, L"\n", &ctx);
    while (line) {
        BOOL crlf = line[0] && line[wcslen(line) - 1] == L'\r';
        /* 检查是否为目标键行（注释剥离后比较） */
        wchar_t tmp[CFG_VAL_LEN * 2];
        lstrcpynW(tmp, line, CFG_VAL_LEN * 2);
        wchar_t* hc = wcschr(tmp, L';');
        wchar_t comment[CFG_VAL_LEN] = L"";
        if (hc) {
            lstrcpynW(comment, hc, CFG_VAL_LEN);
            *hc = 0;
        }
        wchar_t* body = wcs_trim(tmp);
        BOOL is_target = FALSE;
        if (in_sec && !replaced && body[0] != L'[') {
            wchar_t* eq = wcschr(body, L'=');
            if (eq) {
                *eq = 0;
                wchar_t* k = wcs_trim(body);
                if (_wcsicmp(k, key) == 0) is_target = TRUE;
            }
        }
        /* 节头检测 */
        if (body[0] == L'[') {
            wchar_t* end = wcschr(body, L']');
            if (end) {
                *end = 0;
                in_sec = (_wcsicmp(wcs_trim(body + 1), section) == 0);
            }
        }

        if (is_target) {
            wchar_t newline[CFG_VAL_LEN + 128];
            if (comment[0])
                _snwprintf(newline, CFG_VAL_LEN + 128, L"%s=%s %s", key, newval, comment);
            else
                _snwprintf(newline, CFG_VAL_LEN + 128, L"%s=%s", key, newval);
            olen += _snwprintf(out + olen, 32, L"%s%s", newline, crlf ? L"\r\n" : L"\n");
            replaced = TRUE;
        } else {
            olen += _snwprintf(out + olen, 64, L"%s%s", line, crlf ? L"\r\n" : L"\n");
        }
        line = wcstok_s(NULL, L"\n", &ctx);
    }
    free(text);

    /* 未找到该键：追加到节尾 */
    if (!replaced) {
        olen += _snwprintf(out + olen, 64, L"%s=%s\r\n", key, newval);
        replaced = TRUE;
        (void)in_sec;
    }

    /* 以 UTF-8（带 BOM）写回 */
    wchar_t tmppath[MAX_PATH + 8];
    _snwprintf(tmppath, MAX_PATH + 8, L"%s.tmp", ini_path);
    HANDLE h = CreateFileW(tmppath, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { free(out); return FALSE; }
    static const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
    DWORD w = 0;
    WriteFile(h, bom, 3, &w, NULL);
    int need = WideCharToMultiByte(CP_UTF8, 0, out, -1, NULL, 0, NULL, NULL);
    if (need > 0) {
        char* u8 = (char*)malloc((size_t)need);
        if (u8) {
            WideCharToMultiByte(CP_UTF8, 0, out, -1, u8, need, NULL, NULL);
            WriteFile(h, u8, (DWORD)need - 1, &w, NULL);
            free(u8);
        }
    }
    CloseHandle(h);
    free(out);
    BOOL ok = MoveFileExW(tmppath, ini_path, MOVEFILE_REPLACE_EXISTING);
    if (!ok) DeleteFileW(tmppath);
    return ok;
}
