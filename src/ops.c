/* ============================================================
 * ops.c - 操作引擎实现
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <shellapi.h>

#include "ops.h"
#include "gui.h"
#include "util.h"
#include "md5.h"
#include "pattern.h"
#include "device.h"
#include "resource.h"

#ifndef IOCTL_STORAGE_QUERY_PROPERTY
/* CTL_CODE(FILE_DEVICE_MASS_STORAGE=0x07, 0x0500, METHOD_BUFFERED, FILE_ANY_ACCESS) */
#define IOCTL_STORAGE_QUERY_PROPERTY 0x00071400u
#endif

volatile LONG g_stop = 0;

static HWND g_hwnd_result = NULL;
void ops_set_result_target(HWND hwnd) { g_hwnd_result = hwnd; }

/* ---------- 结果投递 ---------- */
static void post_verdict(const wchar_t* verdict)
{
    if (g_hwnd_result)
        PostMessageW(g_hwnd_result, WM_APP_VERDICT, 0, (LPARAM)wcs_dup(verdict));
}

static void post_filecontent(const wchar_t* text, int pane)
{
    if (g_hwnd_result)
        PostMessageW(g_hwnd_result, WM_APP_FILECONTENT, (WPARAM)pane, (LPARAM)wcs_dup(text));
}

static void post_label(const wchar_t* text)
{
    if (g_hwnd_result)
        PostMessageW(g_hwnd_result, WM_APP_LABEL, 0, (LPARAM)wcs_dup(text));
}

static void post_progress(int pct)
{
    if (g_hwnd_result)
        PostMessageW(g_hwnd_result, WM_APP_PROGRESS, (WPARAM)pct, 0);
}

/* 监控状态上报："标题\n副行" */
static void post_moninfo(const wchar_t* title, const wchar_t* sub)
{
    if (g_hwnd_result) {
        wchar_t buf[700];
        _snwprintf(buf, 700, L"%s\n%s", title, sub ? sub : L"");
        PostMessageW(g_hwnd_result, WM_APP_MONINFO, 0, (LPARAM)wcs_dup(buf));
    }
}

static void post_error(BOOL on, const wchar_t* msg)
{
    if (g_hwnd_result)
        PostMessageW(g_hwnd_result, WM_APP_ERROR, on ? 1 : 0, (LPARAM)(msg ? wcs_dup(msg) : NULL));
}

/* 结果置错误状态 */
static void fail(const wchar_t* op, const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 500, fmt, ap);
    va_end(ap);
    applog(CH_ENGINE, LOG_ERR, L"[%s] %s", op, buf);
    post_error(TRUE, buf);
}

/* ============================================================
 * write - 写入（COPY=简单复制；PROCESSED=v1.0 预留提示）
 * ============================================================ */

/* ---------- 复制进度（CopyFileExW 回调：按字节推进，复制大文件时进度条实时移动） ---------- */
typedef struct {
    unsigned __int64 total_all;    /* 本次任务全部文件总字节数 */
    unsigned __int64 done_before;  /* 当前文件之前已完成字节   */
    int last_pct;
} COPYCTX;

static DWORD CALLBACK copy_prog_cb(LARGE_INTEGER TotalFileSize,
                                   LARGE_INTEGER TotalBytesTransferred,
                                   LARGE_INTEGER StreamSize,
                                   LARGE_INTEGER StreamBytesTransferred,
                                   DWORD dwStreamNumber, DWORD dwCallbackReason,
                                   HANDLE hSourceFile, HANDLE hDestinationFile,
                                   LPVOID lpData)
{
    (void)TotalFileSize; (void)StreamSize; (void)StreamBytesTransferred;
    (void)dwStreamNumber; (void)dwCallbackReason;
    (void)hSourceFile; (void)hDestinationFile;
    COPYCTX* c = (COPYCTX*)lpData;
    if (c->total_all) {
        unsigned __int64 done = c->done_before + (unsigned __int64)TotalBytesTransferred.QuadPart;
        int pct = (int)(done * 100 / c->total_all);
        if (pct > 100) pct = 100;
        if (pct != c->last_pct) { c->last_pct = pct; post_progress(pct); }
    }
    return PROGRESS_CONTINUE;
}

int op_write(const CFGMODEL* m, wchar_t drive)
{
    const wchar_t* OP = L"WRITE";
    if (!cfg_enabled(m, L"ENABLE_WRITE")) {
        applog(CH_ENGINE, LOG_WARN, L"[%s] 本机型已禁用写入（ENABLE_WRITE=0），跳过", OP);
        return 0;
    }

    wchar_t mode[32];
    cfg_get_def(m, L"WRITEMODE", L"COPY", mode, 32);
    if (_wcsicmp(mode, L"PROCESSED") == 0) {
        applog(CH_ENGINE, LOG_WARN, L"[%s] PROCESSED 模式（按台生成不同文件）为 v1.x 后续版本功能，本次跳过", OP);
        return 0;
    }

    const wchar_t* srcpat = cfg_get(m, L"SRCPATH");
    if (!srcpat || !srcpat[0]) {
        applog(CH_ENGINE, LOG_WARN, L"[%s] 未配置 SRCPATH，跳过", OP);
        return 0;
    }
    wchar_t dstpath[CFG_VAL_LEN];
    cfg_get_def(m, L"DSTPATH", L"\\", dstpath, CFG_VAL_LEN);

    /* 拆分 SRCPATH：目录 + 通配文件名 */
    wchar_t dir[MAX_PATH];
    lstrcpynW(dir, srcpat, MAX_PATH);
    wchar_t* slash = wcsrchr(dir, L'\\');
    wchar_t fname[MAX_PATH];
    if (slash) {
        lstrcpynW(fname, slash + 1, MAX_PATH);
        *slash = 0;
        if ((!dir[0]) || (dir[1] == L':' && !dir[2])) lstrcatW(dir, L"\\");
        if (dir[wcslen(dir) - 1] != L'\\') lstrcatW(dir, L"\\");
        /* 相对路径 → 以 exe 目录为基准 */
        if (wcschr(dir, L':') == NULL && dir[0] != L'\\') {
            wchar_t abs[MAX_PATH];
            path_join(abs, MAX_PATH, exe_dir(), dir);
            lstrcpynW(dir, abs, MAX_PATH);
        }
    } else {
        lstrcpynW(fname, dir, MAX_PATH);
        path_join(dir, MAX_PATH, exe_dir(), L"");
    }

    /* 归一化路径：解析 "." ".."，相对路径以 exe 所在目录为基准 */
    {
        wchar_t resolved[MAX_PATH];
        DWORD fn = GetFullPathNameW(dir, MAX_PATH, resolved, NULL);
        if (fn > 0 && fn < MAX_PATH) lstrcpynW(dir, resolved, MAX_PATH);
    }
    /* 源目录是否存在：区分"目录不存在"和"目录存在但无匹配文件" */
    {
        DWORD dattr = GetFileAttributesW(dir);
        if (dattr == INVALID_FILE_ATTRIBUTES || !(dattr & FILE_ATTRIBUTE_DIRECTORY)) {
            fail(OP, L"源目录不存在：%s（相对路径以程序目录 %s 为基准，请检查目录名与路径）",
                 dir, exe_dir());
            return 1;
        }
    }

    /* 展开 U 盘目标路径 */
    const wchar_t* dp = dstpath;
    while (*dp == L'\\') dp++;
    wchar_t dstdir[MAX_PATH];
    _snwprintf(dstdir, MAX_PATH, L"%c:\\%s", drive, dp);
    dstdir[MAX_PATH - 1] = 0;

    applog(CH_ENGINE, LOG_INFO, L"[%s] 开始写入：源目录 %s（通配 %s） → %c:\\%s",
           OP, dir, fname, drive, dp);

    /* 目标目录不存在则自动建立（支持多级） */
    if (!util_ensure_dirs(dstdir)) {
        fail(OP, L"无法创建目标目录：%s", dstdir);
        return 1;
    }

    WIN32_FIND_DATAW fd;
    wchar_t findpat[MAX_PATH];
    path_join(findpat, MAX_PATH, dir, fname);
    HANDLE h = FindFirstFileW(findpat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        fail(OP, L"源目录存在，但没有匹配文件（通配符 %s）：%s", fname, findpat);
        return 1;
    }

    int n_ok = 0, n_fail = 0, n_total = 0;
    /* 先计数（只统计文件）并累计总字节数，用于按字节推进度 */
    unsigned __int64 total_bytes = 0, done_bytes = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            n_total++;
            total_bytes += ((unsigned __int64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (n_total == 0) {
        fail(OP, L"源目录内没有可复制的文件：%s（通配 %s）", dir, fname);
        return 1;
    }
    post_progress(0);

    wchar_t hashname[32];
    cfg_get_def(m, L"HASH", L"md5", hashname, 32);
    BOOL use_hash = _wcsicmp(hashname, L"md5") == 0;

    h = FindFirstFileW(findpat, &fd);
    int idx = 0;
    do {
        if (CHECK_STOP()) { applog(CH_ENGINE, LOG_WARN, L"[%s] 已被操作员停止", OP); FindClose(h); return 2; }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        wchar_t srcfile[MAX_PATH], dstfile[MAX_PATH];
        path_join(srcfile, MAX_PATH, dir, fd.cFileName);
        path_join(dstfile, MAX_PATH, dstdir, fd.cFileName);

        /* 目标已存在则覆盖（CopyFileExW 复制中回调按字节上报进度） */
        if (GetFileAttributesW(dstfile) != INVALID_FILE_ATTRIBUTES)
            applog(CH_ENGINE, LOG_INFO, L"[%s] %s 已存在，覆盖", OP, fd.cFileName);

        COPYCTX ctx;
        ctx.total_all = total_bytes;
        ctx.done_before = done_bytes;
        ctx.last_pct = -1;
        if (CopyFileExW(srcfile, dstfile, copy_prog_cb, &ctx, NULL, 0)) {
            done_bytes += ((unsigned __int64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            n_ok++;
            applog(CH_ENGINE, LOG_OK, L"[%s] %s 已复制", OP, fd.cFileName);
            if (use_hash) {
                wchar_t m1[40], m2[40];
                if (sha_or_md5_file(srcfile, m1, 40) && sha_or_md5_file(dstfile, m2, 40)) {
                    if (_wcsicmp(m1, m2) == 0)
                        applog(CH_ENGINE, LOG_OK, L"[%s] MD5 校验一致 (%s...)", OP, m1);
                    else {
                        fail(OP, L"MD5 校验不一致：%s", fd.cFileName);
                        n_fail++;
                    }
                }
            }
        } else {
            fail(OP, L"复制失败 %s（错误码 %lu）", fd.cFileName, GetLastError());
            n_fail++;
        }
        post_progress((++idx) * 100 / n_total);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    post_progress(100);

    if (n_fail > 0) {
        fail(OP, L"写入完成，但有 %d 个文件失败", n_fail);
        return 1;
    }
    applog(CH_ENGINE, LOG_OK, L"[%s] 写入完成：共 %d 个文件", OP, n_ok);
    return 0;
}

/* ============================================================
 * read - 读取 + 判定（READRULE），结果显示
 * ============================================================ */

/* 执行判定规则链，返回 TRUE=PASS；out 输出失败原因 */
static BOOL apply_rules(const wchar_t* rules, const wchar_t* text,
                        wchar_t* why, size_t whyn)
{
    if (!rules || !rules[0]) { why[0] = 0; return TRUE; }
    wchar_t buf[CFG_VAL_LEN];
    lstrcpynW(buf, rules, CFG_VAL_LEN);
    wchar_t* ctx = NULL;
    wchar_t* tok = wcstok_s(buf, L";", &ctx);
    while (tok) {
        wchar_t* t = wcs_trim(tok);
        if (_wcsnicmp(t, L"MUST_CONTAIN:", 13) == 0) {
            const wchar_t* key = t + 13;
            if (!key[0] || !wcsstr(text, key)) {
                _snwprintf(why, whyn, L"内容不包含关键字 \"%s\"", key);
                why[whyn - 1] = 0;
                return FALSE;
            }
        } else if (_wcsnicmp(t, L"MIN_LEN:", 8) == 0) {
            int minlen = _wtoi(t + 8);
            if (minlen > 0 && (int)wcslen(text) < minlen) {
                _snwprintf(why, whyn, L"内容长度 %u 小于最小要求 %d", (unsigned)wcslen(text), minlen);
                why[whyn - 1] = 0;
                return FALSE;
            }
        }
        tok = wcstok_s(NULL, L";", &ctx);
    }
    why[0] = 0;
    return TRUE;
}

int op_read(const CFGMODEL* m, wchar_t drive)
{
    const wchar_t* OP = L"READ";
    if (!cfg_enabled(m, L"ENABLE_READ")) {
        applog(CH_ENGINE, LOG_WARN, L"[%s] 本机型已禁用读取（ENABLE_READ=0），跳过", OP);
        return 0;
    }
    const wchar_t* pat = cfg_get(m, L"READFILE");
    if (!pat || !pat[0]) {
        applog(CH_ENGINE, LOG_INFO, L"[%s] 未配置 READFILE，跳过", OP);
        return 0;
    }

    wchar_t policy[16], ptype[16];
    cfg_get_def(m, L"MATCH_POLICY", L"NEWEST", policy, 16);
    cfg_get_def(m, L"READPATTERN", L"WILDCARD", ptype, 16);
    if (_wcsicmp(ptype, L"REGEX") == 0) {
        applog(CH_ENGINE, LOG_WARN, L"[%s] REGEX 模式为后续版本支持，本次按 WILDCARD 处理", OP);
    }

    /* 扫描 U 盘根目录（READFILE 支持子目录前缀，如 SYSTEM\PAW6K-LSN 或 SYSTEM\*.txt） */
    wchar_t dirpart[CFG_VAL_LEN] = L"", filepat[CFG_VAL_LEN];
    lstrcpynW(filepat, pat, CFG_VAL_LEN);
    {
        wchar_t* sl = wcsrchr(filepat, L'\\');
        if (sl) { *sl = 0; lstrcpynW(dirpart, filepat, CFG_VAL_LEN); lstrcpynW(filepat, sl + 1, CFG_VAL_LEN); }
    }
    wchar_t findpat[MAX_PATH];
    if (dirpart[0])
        _snwprintf(findpat, MAX_PATH, L"%c:\\%s\\%s", drive, dirpart, filepat);
    else
        _snwprintf(findpat, MAX_PATH, L"%c:\\%s", drive, filepat);
    findpat[MAX_PATH - 1] = 0;

    applog(CH_ENGINE, LOG_INFO, L"[%s] 扫描匹配文件：%s（策略 %s）", OP, findpat, policy);

    wchar_t best[MAX_PATH] = L"";      /* 含子目录前缀的相对路径 */
    wchar_t best_name[MAX_PATH] = L""; /* 纯文件名（通配符捕获用） */
    FILETIME best_ft = { 0, 0 };
    wchar_t hits[16][MAX_PATH];        /* 含前缀的相对路径 */
    int nhits = 0;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(findpat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        post_verdict(L"FAIL");
        fail(OP, L"未找到匹配文件（%s）", pat);
        wchar_t empty[2] = L"";
        post_filecontent(empty, 0);
        post_filecontent(empty, 1);
        return 1;
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        wchar_t rel[MAX_PATH];
        if (dirpart[0]) _snwprintf(rel, MAX_PATH, L"%s\\%s", dirpart, fd.cFileName);
        else            lstrcpynW(rel, fd.cFileName, MAX_PATH);
        if (nhits < 16) lstrcpynW(hits[nhits++], rel, MAX_PATH);
        if (_wcsicmp(policy, L"NEWEST") == 0) {
            if (CompareFileTime(&fd.ftLastWriteTime, &best_ft) > 0) {
                best_ft = fd.ftLastWriteTime;
                lstrcpynW(best, rel, MAX_PATH);
                lstrcpynW(best_name, fd.cFileName, MAX_PATH);
            }
        } else if (_wcsicmp(policy, L"FIRST") == 0) {
            if (!best[0]) { lstrcpynW(best, rel, MAX_PATH); lstrcpynW(best_name, fd.cFileName, MAX_PATH); }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (!best[0] && nhits > 0) {
        /* FIRST 之外兜底：取第一个命中（hits 已含前缀，拆回纯文件名） */
        lstrcpynW(best, hits[0], MAX_PATH);
        const wchar_t* bn = wcsrchr(best, L'\\');
        lstrcpynW(best_name, bn ? bn + 1 : best, MAX_PATH);
    }

    if (_wcsicmp(policy, L"STRICT") == 0) {
        if (nhits > 1) {
            post_verdict(L"FAIL");
            applog(CH_ENGINE, LOG_ERR, L"[%s] STRICT 策略：命中 %d 个文件，需人工确认：", OP, nhits);
            for (int i = 0; i < nhits; i++) applog(CH_ENGINE, LOG_ERR, L"  - %s", hits[i]);
            post_error(TRUE, L"STRICT 策略多命中");
            return 1;
        }
    }

    if (!best[0]) {
        post_verdict(L"FAIL");
        fail(OP, L"未找到匹配文件");
        return 1;
    }

    wchar_t fullpath[MAX_PATH];
    _snwprintf(fullpath, MAX_PATH, L"%c:\\%s", drive, best);
    applog(CH_ENGINE, LOG_INFO, L"[%s] 命中文件：%s", OP, best);
    post_moninfo(L"文件已找到", fullpath);

    /* 通配符可变部分捕获（对纯文件名模式与纯文件名比较） */
    wchar_t cap[128] = L"";
    pat_match_capture(filepat, best_name, cap, 128);
    if (cap[0]) applog(CH_ENGINE, LOG_INFO, L"[%s] 可变部分捕获 ${MATCH_1}=%s", OP, cap);

    /* 读取内容 */
    wchar_t* text = NULL;
    if (!read_file_text(fullpath, &text) || !text) {
        post_verdict(L"FAIL");
        fail(OP, L"读取文件失败：%s（错误码 %lu）", best, GetLastError());
        return 1;
    }
    if (CHECK_STOP()) { free(text); return 2; }

    /* 判定 */
    const wchar_t* rules = cfg_get(m, L"READRULE");
    wchar_t why[256];
    BOOL pass = apply_rules(rules, text, why, 256);

    /* 第二命中文件 → 右窗格（IDC_EDIT_FILE2 1503），与主文件同时显示 */
    {
        wchar_t empty[2] = L"";
        post_filecontent(empty, 1);          /* 先清右窗格 */
        for (int i = 0; i < nhits; i++) {
            if (_wcsicmp(hits[i], best) == 0) continue;
            wchar_t p2[MAX_PATH];
            _snwprintf(p2, MAX_PATH, L"%c:\\%s", drive, hits[i]);
            wchar_t* t2 = NULL;
            if (read_file_text(p2, &t2) && t2) {
                applog(CH_ENGINE, LOG_INFO, L"[%s] 第二文件：%s（右窗格）", OP, hits[i]);
                post_filecontent(t2, 1);
                free(t2);
            } else {
                applog(CH_ENGINE, LOG_WARN, L"[%s] 第二文件读取失败：%s（错误码 %lu）", OP, hits[i], GetLastError());
            }
            break;   /* 只取一个第二命中 */
        }
    }

    if (pass) {
        applog(CH_ENGINE, LOG_OK, L"[%s] 判定 PASS（规则：%s）", OP, rules && rules[0] ? rules : L"无");
        post_verdict(L"PASS");
        post_filecontent(text, 0);
    } else {
        applog(CH_ENGINE, LOG_ERR, L"[%s] 判定 FAIL：%s", OP, why);
        post_verdict(L"FAIL");
        wchar_t msg[300];
        _snwprintf(msg, 300, L"判定失败：%s", why);
        post_filecontent(msg, 0);
        post_error(TRUE, why);
        free(text);
        return 1;
    }
    free(text);
    return 0;
}

/* ============================================================
 * format - 格式化（fmifs.dll FormatEx，含确认）
 * ============================================================ */

typedef BOOLEAN(WINAPI* FN_FMIFSCALLBACK)(int Command, int SubCommand, void* Info);
typedef BOOLEAN(WINAPI* FN_FormatEx)(wchar_t* DrivePath, unsigned MediaType,
                                     wchar_t* FileSystem, wchar_t* Label,
                                     BOOL Quick, unsigned ClusterSize,
                                     FN_FMIFSCALLBACK Callback);

static BOOLEAN WINAPI fmifs_cb(int Command, int SubCommand, void* Info)
{
    (void)SubCommand;
    /* 0 = PROGRESS: Info 指向百分比 DWORD */
    if (Command == 0 && Info)
        post_progress((int)(*(DWORD*)Info));
    if (Command == 11 && Info) { /* DONE: Info 指向 BOOLEAN 成功 */ }
    return TRUE;   /* 继续 */
}

int op_format(const CFGMODEL* m, wchar_t drive, HWND hwnd)
{
    const wchar_t* OP = L"FORMAT";
    if (!cfg_enabled(m, L"ENABLE_FORMAT")) {
        applog(CH_ENGINE, LOG_WARN, L"[%s] 本机型已禁用格式化（ENABLE_FORMAT=0），跳过", OP);
        return 0;
    }
    int confirm = cfg_get_int(m, L"CONFIRM", 1);
    if (confirm) {
        wchar_t msg[300];
        _snwprintf(msg, 300, L"确认格式化 %c: 盘？\n\n该盘所有数据将被清除！", drive);
        if (MessageBoxW(hwnd, msg, L"格式化确认", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) {
            applog(CH_ENGINE, LOG_INFO, L"[%s] 操作员取消格式化", OP);
            return 2;
        }
    }

    wchar_t fs[32], volume[64];
    cfg_get_def(m, L"FILESYSTEM", L"FAT32", fs, 32);
    cfg_get_def(m, L"VOLUME", L"", volume, 64);

    applog(CH_ENGINE, LOG_INFO, L"[%s] 开始格式化 %c: (%s, 快速)", OP, drive, fs);

    HMODULE fm = LoadLibraryW(L"fmifs.dll");
    if (!fm) { fail(OP, L"加载 fmifs.dll 失败"); return 1; }
    FN_FormatEx fmt = (FN_FormatEx)GetProcAddress(fm, "FormatEx");
    if (!fmt) { fail(OP, L"找不到 FormatEx 入口"); FreeLibrary(fm); return 1; }

    wchar_t ntdev[16];
    _snwprintf(ntdev, 16, L"\\\\.\\%c:", drive);
    wchar_t label[64];
    lstrcpynW(label, volume, 64);

    g_stop = 0;
    BOOLEAN ok = fmt(ntdev, 0x0C /* FMIFS_HARDDISK */, fs, label, TRUE, 0, fmifs_cb);
    FreeLibrary(fm);
    post_progress(100);

    if (!ok) {
        fail(OP, L"格式化 %c: 失败（如无权限请以管理员身份重试）", drive);
        return 1;
    }

    applog(CH_ENGINE, LOG_OK, L"[%s] 格式化 %c: 完成", OP, drive);

    /* 格式化后写卷标 */
    if (volume[0]) {
        wchar_t root[8];
        _snwprintf(root, 8, L"%c:\\", drive);
        if (SetVolumeLabelW(root, volume))
            applog(CH_ENGINE, LOG_OK, L"[%s] 卷标已写入：%s", OP, volume);
    }
    return 0;
}

/* ============================================================
 * usbinfo - USB 通讯模式识别（USB 2.0 / 3.x，取不到标注未知）
 * ============================================================ */

#define StorageDeviceProtocolSpecificationProperty 51
#define StorageProtocolUSB 2

typedef struct {
    unsigned ProtocolType;
    unsigned DataOffset;
    unsigned DataLength;
    unsigned ProtocolDataRequestValue;
    unsigned ProtocolDataRequestSubValue;
    unsigned ProtocolDataOffset;
    unsigned ProtocolDataLength;
    unsigned FixedProtocolReturnData;
    unsigned Reserved[3];
} PROTO_DATA;

static const wchar_t* usb_speed_name(unsigned speed)
{
    switch (speed) {
    case 1: return L"USB Low-Speed (1.5Mbps)";
    case 2: return L"USB Full-Speed (12Mbps)";
    case 3: return L"USB 2.0 High-Speed (480Mbps)";
    case 4: return L"USB 3.0 SuperSpeed (5Gbps)";
    case 5: return L"USB 3.1 SuperSpeed+ (10Gbps)";
    default: return NULL;
    }
}

int op_usbinfo(const CFGMODEL* m, wchar_t drive)
{
    const wchar_t* OP = L"USBINFO";
    if (!cfg_enabled(m, L"ENABLE_USBINFO")) {
        applog(CH_ENGINE, LOG_WARN, L"[%s] 本机型已禁用 USB 识别（ENABLE_USBINFO=0），跳过", OP);
        return 0;
    }

    DEVINFO di;
    dev_get_info(drive, &di);
    applog(CH_ENGINE, LOG_INFO, L"[%s] %c: 卷标=%s 文件系统=%s 容量=%.1fGB",
           OP, drive, di.volume[0] ? di.volume : L"(无)", di.fs,
           di.total_bytes / (1024.0 * 1024 * 1024));
    if (di.has_serial)
        applog(CH_ENGINE, LOG_INFO, L"[%s] 卷序列号=%08lX", OP, di.serial);

    /* 尝试协议规格查询（Win8+，失败不报错） */
    const wchar_t* speed = NULL;
    HANDLE h = CreateFileW(di.root, 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        unsigned char inbuf[128] = { 0 };
        unsigned char outbuf[512] = { 0 };
        /* STORAGE_PROPERTY_QUERY: PropertyId, QueryType, AdditionalParameters */
        ((unsigned*)inbuf)[0] = StorageDeviceProtocolSpecificationProperty;
        ((unsigned*)inbuf)[1] = 0;  /* PropertyStandardQuery */
        PROTO_DATA* pd = (PROTO_DATA*)(inbuf + 8);
        pd->ProtocolType = StorageProtocolUSB;
        pd->DataOffset = sizeof(PROTO_DATA);
        pd->DataLength = 64;
        DWORD bytes = 0;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, inbuf, sizeof(inbuf),
                            outbuf, sizeof(outbuf), &bytes, NULL) && bytes >= 32) {
            /* STORAGE_PROTOCOL_DATA_DESCRIPTOR: Version, Size, Specific(PROTO_DATA)
             * USB 数据从 DataOffset 起按 {Version, Size, Speed,...} 尝试解释 */
            PROTO_DATA* rsp = (PROTO_DATA*)(outbuf + 8);
            if (rsp->ProtocolDataOffset > 0 && rsp->ProtocolDataLength >= 12 &&
                rsp->ProtocolDataOffset + rsp->ProtocolDataLength <= bytes) {
                unsigned* uinfo = (unsigned*)(outbuf + 8 + rsp->ProtocolDataOffset);
                speed = usb_speed_name(uinfo[2]);
            }
        }
        CloseHandle(h);
    }

    if (speed) {
        applog(CH_ENGINE, LOG_OK, L"[%s] 通讯模式：%s", OP, speed);
    } else {
        applog(CH_ENGINE, LOG_WARN, L"[%s] 通讯模式：未知（当前系统接口无法查询，不判定为错误）", OP);
    }
    applog(CH_ENGINE, LOG_INFO, L"[%s] Type-C 正反插识别：预留（需 USB 事件监听扩展）", OP);
    return 0;
}

/* ============================================================
 * 标签读写
 * ============================================================ */

int op_label_read(const CFGMODEL* m, wchar_t drive)
{
    (void)m;
    DEVINFO di;
    if (!dev_get_info(drive, &di)) {
        fail(L"LABEL", L"读取 %c: 标签失败", drive);
        return 1;
    }
    wchar_t txt[512];
    _snwprintf(txt, 512, L"%c:  卷标=\"%s\"  文件系统=%s  序列号=%08lX  容量=%.2fGB  可用=%.2fGB",
               drive, di.volume[0] ? di.volume : L"(无)", di.fs,
               di.serial, di.total_bytes / 1073741824.0, di.free_bytes / 1073741824.0);
    applog(CH_ENGINE, LOG_OK, L"[LABEL] %s", txt);
    post_label(txt);
    return 0;
}

/* ============================================================
 * rwtest - 读写测试（写 8MB 测试文件 → 回读 → MD5 比对 → 速率）
 * ============================================================ */

int op_rwtest(const CFGMODEL* m, wchar_t drive)
{
    const wchar_t* OP = L"RWTEST";
    applog(CH_ENGINE, LOG_INFO, L"[%s] %c: 开始读写测试（8MB，写→读→MD5 比对）", OP, drive);

    const DWORD SIZE = 8 * 1024 * 1024;
    unsigned char* wbuf = (unsigned char*)malloc(SIZE);
    unsigned char* rbuf = (unsigned char*)malloc(SIZE);
    if (!wbuf || !rbuf) { free(wbuf); free(rbuf); fail(OP, L"内存不足"); return 1; }

    srand((unsigned)time(NULL) ^ (unsigned)drive);
    for (DWORD i = 0; i < SIZE; i++) wbuf[i] = (unsigned char)(rand() & 0xFF);
    unsigned char wdigest[16], rdigest[16];
    {
        md5_context c;
        md5_init(&c);
        md5_update(&c, wbuf, SIZE);
        md5_final(&c, wdigest);
    }

    wchar_t path[MAX_PATH];
    _snwprintf(path, MAX_PATH, L"%c:\\udk_rwtest.bin", drive);
    (void)m;

    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceFrequency(&freq);

    /* 写 */
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fail(OP, L"无法创建测试文件（错误码 %lu）", GetLastError());
        free(wbuf); free(rbuf);
        return 1;
    }
    QueryPerformanceCounter(&t0);
    DWORD wr = 0;
    BOOL ok = TRUE;
    for (DWORD off = 0; off < SIZE; off += 64 * 1024) {
        if (CHECK_STOP()) { ok = FALSE; break; }
        DWORD w = 0;
        if (!WriteFile(h, wbuf + off, 64 * 1024, &w, NULL)) { ok = FALSE; break; }
        wr += w;
    }
    FlushFileBuffers(h);
    QueryPerformanceCounter(&t1);
    double wsec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    CloseHandle(h);
    if (!ok) {
        fail(OP, L"写入中断（可能拔盘或写保护）");
        DeleteFileW(path);
        free(wbuf); free(rbuf);
        return 1;
    }
    double wmbps = wr / 1048576.0 / (wsec > 0 ? wsec : 0.001);
    applog(CH_ENGINE, LOG_INFO, L"[%s] 写入 %.2f MB/s", OP, wmbps);

    /* 读 */
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fail(OP, L"无法回读测试文件");
        DeleteFileW(path);
        free(wbuf); free(rbuf);
        return 1;
    }
    QueryPerformanceCounter(&t0);
    DWORD rr = 0;
    ok = TRUE;
    for (DWORD off = 0; off < SIZE; off += 64 * 1024) {
        if (CHECK_STOP()) { ok = FALSE; break; }
        DWORD r = 0;
        if (!ReadFile(h, rbuf + off, 64 * 1024, &r, NULL)) { ok = FALSE; break; }
        rr += r;
    }
    QueryPerformanceCounter(&t1);
    double rsec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    CloseHandle(h);
    if (!ok || rr != wr) {
        fail(OP, L"回读字节数不符（%lu/%lu）", rr, wr);
        DeleteFileW(path);
        free(wbuf); free(rbuf);
        return 1;
    }
    double rmbps = rr / 1048576.0 / (rsec > 0 ? rsec : 0.001);
    applog(CH_ENGINE, LOG_INFO, L"[%s] 读取 %.2f MB/s", OP, rmbps);

    {
        md5_context c;
        md5_init(&c);
        md5_update(&c, rbuf, rr);
        md5_final(&c, rdigest);
    }
    BOOL same = memcmp(wdigest, rdigest, 16) == 0;
    DeleteFileW(path);
    free(wbuf); free(rbuf);

    if (same) {
        applog(CH_ENGINE, LOG_OK, L"[%s] 读写比对一致，测试通过", OP);
        post_progress(100);
        return 0;
    }
    fail(OP, L"读写比对不一致，U 盘可能存在坏块！");
    return 1;
}

/* ============================================================
 * gen_ident - 标识生成输出（CMD-B 通道，Phase 3 完整化）
 * ============================================================ */

int op_gen_ident(const CFGMODEL* m, wchar_t drive, int index)
{
    applog(CH_IDENT, LOG_INFO, L"[IDENT] 机型=%s 盘=%c: 第 %d 台生成标识",
           m->internal, drive, index);
    __time64_t t = _time64(NULL);
    struct tm lt;
    _localtime64_s(&lt, &t);
    wchar_t sn[64];
    _snwprintf(sn, 64, L"%s%04d%02d%02d-%03d", m->internal,
               lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, index);
    wchar_t mac[32];
    srand((unsigned)t ^ index);
    _snwprintf(mac, 32, L"02-%02X-%02X-%02X-%02X-%02X",
               rand() & 0xFF, rand() & 0xFF, rand() & 0xFF, rand() & 0xFF, rand() & 0xFF);
    applog(CH_IDENT, LOG_OK, L"[IDENT] SN=%s", sn);
    applog(CH_IDENT, LOG_OK, L"[IDENT] MAC=%s", mac);
    applog(CH_IDENT, LOG_INFO, L"[IDENT] （生成规则按配置/外部命令驱动，v1.0 为演示输出）");
    return 0;
}
