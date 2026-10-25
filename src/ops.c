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
#include "usbspeed.h"
#include "resource.h"

volatile LONG g_stop = 0;
/* CopyFileExW 的取消标志（与 g_stop 由 ops_request_stop/ops_reset_stop 统一管理） */
static volatile LONG g_copy_cancel = 0;

void ops_request_stop(void)
{
    InterlockedExchange(&g_stop, 1);
    InterlockedExchange(&g_copy_cancel, 1);
}

void ops_reset_stop(void)
{
    InterlockedExchange(&g_copy_cancel, 0);
    InterlockedExchange(&g_stop, 0);
}

static HWND g_hwnd_result = NULL;
void ops_set_result_target(HWND hwnd) { g_hwnd_result = hwnd; }

/* ---------- 结果投递 ----------
 * P2-9：所有“堆串随消息投递”统一走 util.h 的 post_owned_string：
 *   复制文本 → PostMessage；投递失败由 helper 自行释放并返回 FALSE（调用方不得再 free）。
 *   接收端（gui.c）仍按原约定负责释放成功投递的 lparam。 */
static void post_verdict(const wchar_t* verdict)
{
    if (g_hwnd_result &&
        !post_owned_string(g_hwnd_result, WM_APP_VERDICT, 0, verdict))
        filelog(LOG_WARN, L"[OPS] 投递判定结果失败（结果窗口不可用）");
}

static void post_filecontent(const wchar_t* text, int pane)
{
    if (g_hwnd_result &&
        !post_owned_string(g_hwnd_result, WM_APP_FILECONTENT, (WPARAM)pane, text))
        filelog(LOG_WARN, L"[OPS] 投递文件内容失败（结果窗口不可用）");
}

static void post_label(const wchar_t* text)
{
    if (g_hwnd_result &&
        !post_owned_string(g_hwnd_result, WM_APP_LABEL, 0, text))
        filelog(LOG_WARN, L"[OPS] 投递标签文本失败（结果窗口不可用）");
}

static void post_progress(int pct)
{
    if (g_hwnd_result)
        PostMessageW(g_hwnd_result, WM_APP_PROGRESS, (WPARAM)pct, 0);
}

/* 格式化等操作完成后，请求 GUI 重新采集并刷新设备/标签行（设备信息已变化） */
static void post_devrefresh(void)
{
    if (g_hwnd_result)
        PostMessageW(g_hwnd_result, WM_APP_DEVREFRESH, 0, 0);
}

/* 监控状态上报："标题\n副行"；pane=1 时只写右窗格专属提示行（IDC_STC_MONFILE2 1108） */
static void post_moninfo(const wchar_t* title, const wchar_t* sub, int pane)
{
    if (g_hwnd_result) {
        wchar_t buf[700];
        _snwprintf(buf, 700, L"%s\n%s", title ? title : L"", sub ? sub : L"");
        if (!post_owned_string(g_hwnd_result, WM_APP_MONINFO, (WPARAM)pane, buf))
            filelog(LOG_WARN, L"[OPS] 投递监控状态失败（结果窗口不可用）");
    }
}

static void post_error(BOOL on, const wchar_t* msg)
{
    if (!g_hwnd_result) return;
    if (msg) {
        if (!post_owned_string(g_hwnd_result, WM_APP_ERROR, on ? 1 : 0, msg))
            filelog(LOG_WARN, L"[OPS] 投递错误徽标失败（结果窗口不可用）");
    } else {
        /* 无文本：仅投递徽标状态（熄灭），lparam 为 NULL，接收端 free(NULL) 安全 */
        PostMessageW(g_hwnd_result, WM_APP_ERROR, on ? 1 : 0, 0);
    }
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

/* 设备丢失类错误：盘被拔出/未就绪/介质不在（P1-5：与普通 IO 错误区分） */
static BOOL dev_lost_error(DWORD err)
{
    return err == ERROR_NOT_READY || err == ERROR_DEVICE_NOT_CONNECTED ||
           err == ERROR_NO_MEDIA_IN_DRIVE;
}

/* 设备/IO 错误分类：设备丢失 / 写保护 / 空间不足 / 其它 */
static void fail_io(const wchar_t* op, const wchar_t* what, DWORD err)
{
    if (dev_lost_error(err))
        fail(op, L"%s：设备已移除/未就绪（错误码 %lu）", what, err);
    else if (err == ERROR_WRITE_PROTECT)
        fail(op, L"%s：磁盘写保护（错误码 %lu）", what, err);
    else if (err == ERROR_DISK_FULL || err == ERROR_HANDLE_DISK_FULL)
        fail(op, L"%s：磁盘空间不足（错误码 %lu）", what, err);
    else
        fail(op, L"%s（错误码 %lu）", what, err);
}

/* ============================================================
 * 设备身份复核（任务级基线 expect 的进入 / 关键点复核）
 * ============================================================ */

/* 复核 expect 与当前设备是否一致：返回 0=一致或未提供基线，1=已变化（应中止） */
static int check_expect(const wchar_t* op, wchar_t drive, const DEVSIG* expect)
{
    if (!expect || !expect->drive) return 0;   /* 无基线：不阻塞 */
    DEVSIG cur;
    if (!dev_capture_sig(drive, &cur)) {
        wchar_t a[256];
        dev_sig_describe(expect, a, 256);
        fail(op, L"目标盘已变化（原 %s / 现 无法读取），任务中止", a);
        return 1;
    }
    if (!dev_sig_same_device(expect, &cur)) {
        wchar_t a[256], b[256];
        dev_sig_describe(expect, a, 256);
        dev_sig_describe(&cur, b, 256);
        fail(op, L"目标盘已变化（原 %s / 现 %s），任务中止", a, b);
        return 1;
    }
    return 0;
}

/* 轻量卷序列号复核（仅 GetVolumeInformationW，不跑 SetupAPI）；返回 0=一致，1=已变化 */
static int check_volume_serial(const wchar_t* op, wchar_t drive, DWORD serial)
{
    wchar_t root[8];
    _snwprintf(root, 8, L"%c:\\", drive);
    DWORD cur = 0;
    if (!GetVolumeInformationW(root, NULL, 0, &cur, NULL, NULL, NULL, 0)) {
        fail(op, L"目标盘已无法访问（可能已拔出或未就绪），任务中止");
        return 1;
    }
    if (cur != serial) {
        fail(op, L"目标盘卷序列号已变化（原 %08lX / 现 %08lX），任务中止",
             (unsigned long)serial, (unsigned long)cur);
        return 1;
    }
    return 0;
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
    if (CHECK_STOP()) return PROGRESS_CANCEL;   /* [停止] 立即中断复制 */
    COPYCTX* c = (COPYCTX*)lpData;
    if (c->total_all) {
        unsigned __int64 done = c->done_before + (unsigned __int64)TotalBytesTransferred.QuadPart;
        int pct = (int)(done * 100 / c->total_all);
        if (pct > 100) pct = 100;
        if (pct != c->last_pct) { c->last_pct = pct; post_progress(pct); }
    }
    return PROGRESS_CONTINUE;
}

/* MD5 十六进制文本缓冲长度（32 字符 + NUL，留余量） */
#define MD5_HEX_CCH 40

/* 计算源/目标文件 MD5：返回 1=一致，2=不一致，0=无法计算。
 * 无法计算时 *fail_side 写"源"/"目标"，*fail_err 写失败错误码。 */
static int md5_compare_copy(const wchar_t* srcfile, const wchar_t* dstfile,
                            wchar_t* m1, wchar_t* m2,
                            const wchar_t** fail_side, DWORD* fail_err)
{
    *fail_side = L"源";
    *fail_err = 0;
    if (!sha_or_md5_file(srcfile, m1, MD5_HEX_CCH)) {
        *fail_err = GetLastError();
        return 0;
    }
    *fail_side = L"目标";
    if (!sha_or_md5_file(dstfile, m2, MD5_HEX_CCH)) {
        *fail_err = GetLastError();
        return 0;
    }
    return _wcsicmp(m1, m2) == 0 ? 1 : 2;
}

/* 删除校验失败/无法校验的目标副本；删除失败只 WARN（不升级为新的错误类型） */
static void delete_bad_copy(const wchar_t* op, const wchar_t* path)
{
    if (DeleteFileW(path)) return;
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND) return;
    applog(CH_ENGINE, LOG_WARN, L"[%s] 无法删除目标文件 %s（错误码 %lu）", op, path, err);
}

int op_write(const CFGMODEL* m, wchar_t drive, const DEVSIG* expect)
{
    const wchar_t* OP = L"WRITE";
    /* 配置正常屏蔽：按用户判定政策仍属“成功类别”，但单独计数（OP_SKIP_CFG） */
    if (!cfg_enabled(m, L"ENABLE_WRITE")) {
        applog(CH_ENGINE, LOG_INFO,
               L"[%s] 按配置禁用写入（ENABLE_WRITE=0），跳过（按配置正常屏蔽）", OP);
        return OP_SKIP_CFG;
    }

    /* 进入复核：任务基线（expect）与当前设备不一致则不做任何写入 */
    if (expect && expect->drive && check_expect(OP, drive, expect)) return OP_FAIL;

    /* 身份基线：优先用任务基线；否则自采（自采失败不阻塞任务，仅跳过卷序列号复核） */
    DEVSIG base;
    BOOL have_base = FALSE;
    if (expect && expect->drive) {
        base = *expect;
        have_base = TRUE;
    } else if (dev_capture_sig(drive, &base)) {
        have_base = TRUE;
    } else {
        applog(CH_ENGINE, LOG_WARN,
               L"[%s] 未能采集目标盘身份基线，本次不做逐文件卷序列号复核", OP);
    }

    wchar_t mode[32];
    cfg_get_def(m, L"WRITEMODE", L"COPY", mode, 32);
    if (_wcsicmp(mode, L"PROCESSED") == 0) {
        /* 程序自行降级/未实现：不得按成功处理，必须显式失败（红徽标） */
        fail(OP, L"WRITEMODE=PROCESSED 未实现，不能按成功处理（本次不执行写入）");
        return OP_FAIL;
    }

    const wchar_t* srcpat = cfg_get(m, L"SRCPATH");
    if (!srcpat || !srcpat[0]) {
        /* ENABLE_WRITE=1 却无源路径 = 配置不完整，不属于“按配置正常屏蔽” */
        fail(OP, L"ENABLE_WRITE=1 但未配置 SRCPATH，无法执行写入（配置不完整，不能按正常屏蔽处理）");
        return OP_FAIL;
    }

    /* HASH 只实现了 md5：其它算法的“复制后校验”无法完成，拒绝在未校验状态下复制 */
    wchar_t hashname[32];
    cfg_get_def(m, L"HASH", L"md5", hashname, 32);
    BOOL use_hash = _wcsicmp(hashname, L"md5") == 0;
    if (!use_hash) {
        fail(OP, L"HASH=%s 未实现/不支持，拒绝在未校验状态下复制（仅支持 md5）", hashname);
        return OP_FAIL;
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
            return OP_FAIL;
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

    WIN32_FIND_DATAW fd;
    wchar_t findpat[MAX_PATH];
    path_join(findpat, MAX_PATH, dir, fname);
    HANDLE h = FindFirstFileW(findpat, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        fail(OP, L"源目录存在，但没有匹配文件（通配符 %s）：%s", fname, findpat);
        return OP_FAIL;
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
        return OP_FAIL;
    }

    /* P3-3：只有确实匹配到 ≥1 个源文件后才创建目标目录（支持多级），
     * 避免源目录为空/无匹配文件时在 U 盘上留下空目录；
     * 匹配到文件但建目录失败仍走原失败路径。 */
    if (!util_ensure_dirs(dstdir)) {
        fail(OP, L"无法创建目标目录：%s", dstdir);
        return OP_FAIL;
    }
    post_progress(0);

    h = FindFirstFileW(findpat, &fd);
    int idx = 0;
    do {
        if (CHECK_STOP()) { applog(CH_ENGINE, LOG_WARN, L"[%s] 已被操作员停止", OP); FindClose(h); return OP_STOPPED; }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        /* 每复制一个文件前轻量复核卷序列号（防中途换盘/重插其他盘），不一致立即中止 */
        if (have_base && base.has_serial &&
            check_volume_serial(OP, drive, base.volume_serial)) {
            FindClose(h);
            return OP_FAIL;
        }

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
        if (CopyFileExW(srcfile, dstfile, copy_prog_cb, &ctx,
                        (LPBOOL)&g_copy_cancel, 0)) {
            done_bytes += ((unsigned __int64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            applog(CH_ENGINE, LOG_OK, L"[%s] %s 已复制", OP, fd.cFileName);
            if (!use_hash) {
                n_ok++;
            } else {
                wchar_t m1[MD5_HEX_CCH], m2[MD5_HEX_CCH];
                const wchar_t* side = NULL;
                DWORD herr = 0;
                int cmp = md5_compare_copy(srcfile, dstfile, m1, m2, &side, &herr);
                if (cmp == 1) {
                    /* 只有最终校验通过后才计入成功数 */
                    n_ok++;
                    applog(CH_ENGINE, LOG_OK, L"[%s] MD5 校验一致 (%s...)", OP, m1);
                } else if (cmp == 0) {
                    /* 无法校验不能按已校验成功处理；坏副本删除，避免留下未校验文件 */
                    fail(OP, L"MD5 校验无法完成（无法计算%s文件 MD5，错误码 %lu）：%s，不能按已校验处理",
                         side, herr, fd.cFileName);
                    delete_bad_copy(OP, dstfile);
                    n_fail++;
                } else {
                    /* 不一致：删除坏副本，重试复制一次（重试期间不重复推进进度条） */
                    applog(CH_ENGINE, LOG_WARN,
                           L"[%s] %s MD5 不一致，删除目标并重试复制一次", OP, fd.cFileName);
                    delete_bad_copy(OP, dstfile);
                    if (CHECK_STOP()) {
                        applog(CH_ENGINE, LOG_WARN, L"[%s] 已被操作员停止", OP);
                        FindClose(h);
                        return OP_STOPPED;
                    }
                    COPYCTX rctx;
                    rctx.total_all = 0;      /* 0 = 不推进进度（本文件进度只算一次） */
                    rctx.done_before = 0;
                    rctx.last_pct = -1;
                    if (CopyFileExW(srcfile, dstfile, copy_prog_cb, &rctx,
                                    (LPBOOL)&g_copy_cancel, 0)) {
                        cmp = md5_compare_copy(srcfile, dstfile, m1, m2, &side, &herr);
                        if (cmp == 1) {
                            n_ok++;
                            applog(CH_ENGINE, LOG_OK,
                                   L"[%s] %s 重试后校验一致 (%s...)", OP, fd.cFileName, m1);
                        } else if (cmp == 0) {
                            fail(OP, L"MD5 校验无法完成（重试后仍无法计算%s文件 MD5，错误码 %lu）：%s，不能按已校验处理",
                                 side, herr, fd.cFileName);
                            delete_bad_copy(OP, dstfile);
                            n_fail++;
                        } else {
                            fail(OP, L"MD5 重试后仍不一致：%s", fd.cFileName);
                            delete_bad_copy(OP, dstfile);
                            n_fail++;
                        }
                    } else {
                        DWORD err = GetLastError();
                        if (err == ERROR_REQUEST_ABORTED || CHECK_STOP()) {
                            applog(CH_ENGINE, LOG_WARN, L"[%s] 已停止（重试复制被取消）", OP);
                            delete_bad_copy(OP, dstfile);
                            FindClose(h);
                            return OP_STOPPED;
                        }
                        if (dev_lost_error(err)) {
                            /* 设备丢失类错误：立即中止，不再继续后续文件 */
                            fail(OP, L"设备已移除，写入中止（%s，错误码 %lu）", fd.cFileName, err);
                            FindClose(h);
                            return OP_FAIL;
                        }
                        fail(OP, L"MD5 不一致后重试复制失败 %s（错误码 %lu）", fd.cFileName, err);
                        n_fail++;
                    }
                }
            }
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_REQUEST_ABORTED || CHECK_STOP()) {
                /* 复制被 [停止] 取消：按"已停止"处理，不记为复制失败 */
                applog(CH_ENGINE, LOG_WARN, L"[%s] 已停止（复制被取消）", OP);
                FindClose(h);
                return OP_STOPPED;
            }
            if (err == ERROR_NOT_READY || err == ERROR_DEVICE_NOT_CONNECTED ||
                err == ERROR_NO_MEDIA_IN_DRIVE) {
                /* 设备丢失类错误：立即中止，不再继续复制后续文件 */
                fail(OP, L"设备已移除，写入中止（%s，错误码 %lu）", fd.cFileName, err);
                FindClose(h);
                return OP_FAIL;
            }
            fail(OP, L"复制失败 %s（错误码 %lu）", fd.cFileName, err);
            n_fail++;
        }
        post_progress((++idx) * 100 / n_total);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    post_progress(100);

    if (n_fail > 0) {
        fail(OP, L"写入完成，但有 %d 个文件失败", n_fail);
        return OP_FAIL;
    }
    applog(CH_ENGINE, LOG_OK, L"[%s] 写入完成：共 %d 个文件", OP, n_ok);
    return OP_OK;
}

/* ============================================================
 * read - 读取 + 判定（READRULE），结果显示
 * ============================================================ */

/* 执行判定规则链：返回 1=PASS，0=规则不满足（why=原因），-1=未识别的规则 token（why=原因）。
 * 空 READRULE / 空 token 不算未识别。 */
static int apply_rules(const wchar_t* rules, const wchar_t* text,
                       wchar_t* why, size_t whyn)
{
    why[0] = 0;
    if (!rules || !rules[0]) return 1;   /* 空 READRULE = 按配置不做内容判定 → PASS */
    wchar_t buf[CFG_VAL_LEN];
    lstrcpynW(buf, rules, CFG_VAL_LEN);
    wchar_t* ctx = NULL;
    wchar_t* tok = wcstok_s(buf, L";", &ctx);
    while (tok) {
        wchar_t* t = wcs_trim(tok);
        if (t[0]) {   /* 空 token（如 ";;" 或纯空白）不算未识别规则 */
            if (_wcsnicmp(t, L"MUST_CONTAIN:", 13) == 0) {
                const wchar_t* key = t + 13;
                if (!key[0] || !wcsstr(text, key)) {
                    _snwprintf(why, whyn, L"内容不包含关键字 \"%s\"", key);
                    why[whyn - 1] = 0;
                    return 0;
                }
            } else if (_wcsnicmp(t, L"MIN_LEN:", 8) == 0) {
                int minlen = _wtoi(t + 8);
                if (minlen > 0 && (int)wcslen(text) < minlen) {
                    _snwprintf(why, whyn, L"内容长度 %u 小于最小要求 %d", (unsigned)wcslen(text), minlen);
                    why[whyn - 1] = 0;
                    return 0;
                }
            } else {
                /* 程序自行降级：未识别规则既不能当作通过，也不能静默忽略 */
                _snwprintf(why, whyn, L"未识别的判定规则 \"%s\"", t);
                why[whyn - 1] = 0;
                return -1;
            }
        }
        tok = wcstok_s(NULL, L";", &ctx);
    }
    return 1;
}

int op_read(const CFGMODEL* m, wchar_t drive)
{
    const wchar_t* OP = L"READ";
    /* 配置正常屏蔽：按用户判定政策仍属“成功类别”，但单独计数；不触碰上一轮界面状态 */
    if (!cfg_enabled(m, L"ENABLE_READ")) {
        applog(CH_ENGINE, LOG_INFO,
               L"[%s] 按配置禁用读取（ENABLE_READ=0），跳过（按配置正常屏蔽）", OP);
        return OP_SKIP_CFG;
    }
    const wchar_t* pat = cfg_get(m, L"READFILE");
    if (!pat || !pat[0]) {
        applog(CH_ENGINE, LOG_INFO,
               L"[%s] READFILE 未配置/为空（手册约定留空=跳过读取），跳过（按配置正常屏蔽）", OP);
        return OP_SKIP_CFG;
    }

    /* 入口采集身份基线（成功才做读取后复核；失败只 WARN，不因此失败） */
    DEVSIG sig;
    BOOL have_sig = dev_capture_sig(drive, &sig);
    if (!have_sig)
        applog(CH_ENGINE, LOG_WARN,
               L"[%s] 未能采集目标盘身份基线，本次不做读取后复核", OP);

    wchar_t policy[16], ptype[16];
    cfg_get_def(m, L"MATCH_POLICY", L"NEWEST", policy, 16);
    cfg_get_def(m, L"READPATTERN", L"WILDCARD", ptype, 16);
    if (_wcsicmp(ptype, L"REGEX") == 0) {
        /* 程序自行降级：原实现静默按 WILDCARD 处理 → 现拒绝降级判定 */
        post_verdict(L"FAIL");
        fail(OP, L"READPATTERN=REGEX 未实现，拒绝按 WILDCARD 降级判定（不能按成功处理）");
        return OP_FAIL;
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
        return OP_FAIL;
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
            return OP_FAIL;
        }
    }

    if (!best[0]) {
        post_verdict(L"FAIL");
        fail(OP, L"未找到匹配文件");
        return OP_FAIL;
    }

    wchar_t fullpath[MAX_PATH];
    _snwprintf(fullpath, MAX_PATH, L"%c:\\%s", drive, best);
    applog(CH_ENGINE, LOG_INFO, L"[%s] 命中文件：%s", OP, best);
    post_moninfo(L"文件已找到", fullpath, 0);

    /* 通配符可变部分捕获（对纯文件名模式与纯文件名比较）。
     * P2-16 残余：捕获超出 cap 容量时不再静默截断，WARN 给出总长度与前缀。 */
    wchar_t cap[128] = L"";
    BOOL cap_truncated = FALSE;
    pat_match_capture_ex(filepat, best_name, cap, 128, &cap_truncated);
    if (cap_truncated) {
        unsigned cap_whole = 0;      /* 每个 '?' 捕获 1 个字符，总数即完整捕获长度 */
        const wchar_t* cp;
        for (cp = filepat; *cp; cp++)
            if (*cp == L'?') cap_whole++;
        applog(CH_ENGINE, LOG_WARN,
               L"[%s] 可变部分捕获超长已截断（%u 字符），${MATCH_1} 仅记录前 %u 字符：%s",
               OP, cap_whole, (unsigned)wcslen(cap), cap);
    } else if (cap[0]) {
        applog(CH_ENGINE, LOG_INFO, L"[%s] 可变部分捕获 ${MATCH_1}=%s", OP, cap);
    }

    /* 读取内容 */
    wchar_t* text = NULL;
    if (!read_file_text(fullpath, &text) || !text) {
        DWORD err = GetLastError();
        DEVSIG cur;
        BOOL still = dev_capture_sig(drive, &cur) &&
                     (!have_sig || dev_sig_same_device(&sig, &cur));
        post_verdict(L"FAIL");
        if (!still)
            fail(OP, L"读取文件失败：%s（设备已移除或未就绪）", best);
        else
            fail(OP, L"读取文件失败：%s（错误码 %lu）", best, err);
        return OP_FAIL;
    }
    if (CHECK_STOP()) { free(text); return OP_STOPPED; }

    /* P1-5 残余：读取后复核设备签名 —— 读取过程中换盘/拔出则本次判定作废 */
    if (have_sig) {
        DEVSIG cur;
        if (!dev_capture_sig(drive, &cur) || !dev_sig_same_device(&sig, &cur)) {
            free(text);
            post_verdict(L"FAIL");
            fail(OP, L"读取过程中目标盘已变化/已移除，判定作废（不能按成功处理）");
            return OP_FAIL;
        }
    }

    /* 判定 */
    const wchar_t* rules = cfg_get(m, L"READRULE");
    wchar_t why[256];
    int rr = apply_rules(rules, text, why, 256);
    if (rr < 0) {
        /* 未识别规则：程序自行降级，不能按成功处理，也不得静默忽略 */
        free(text);
        post_verdict(L"FAIL");
        fail(OP, L"%s（未实现的判定规则不能静默忽略）", why);
        return OP_FAIL;
    }
    BOOL pass = (rr == 1);

    /* 第二命中文件 → 右窗格（IDC_EDIT_FILE2 1503），与主文件同时显示 */
    {
        wchar_t empty[2] = L"";
        post_filecontent(empty, 1);          /* 先清右窗格 */
        post_moninfo(L"第二文件", L"", 1);   /* 清右窗格专属提示行 */
        for (int i = 0; i < nhits; i++) {
            if (_wcsicmp(hits[i], best) == 0) continue;
            wchar_t p2[MAX_PATH];
            _snwprintf(p2, MAX_PATH, L"%c:\\%s", drive, hits[i]);
            wchar_t* t2 = NULL;
            if (read_file_text(p2, &t2) && t2) {
                applog(CH_ENGINE, LOG_INFO, L"[%s] 第二文件：%s（右窗格）", OP, hits[i]);
                post_filecontent(t2, 1);
                post_moninfo(L"第二文件", p2, 1);
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
        return OP_FAIL;
    }
    free(text);
    return OP_OK;
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

/* 构造标签行文本（op_label_read 与 op_format 格式化后刷新共用，格式须一致） */
static void build_label_text(wchar_t drive, const DEVINFO* di, wchar_t* out, size_t outlen)
{
    _snwprintf(out, outlen,
               L"%c:  卷标=\"%s\"  文件系统=%s  序列号=%08lX  容量=%.2fGB  可用=%.2fGB",
               drive,
               (di->volume[0]) ? di->volume : L"(无)",
               di->fs[0] ? di->fs : L"未知",
               (unsigned long)di->serial,
               di->total_bytes / 1073741824.0,
               di->free_bytes / 1073741824.0);
    out[outlen - 1] = 0;
}

/* 格式化后刷新设备信息：失败时重试 3 次 × 200ms。
 * 以"成功读到卷信息（has_serial）"为刷新成功——仅返回 TRUE 不足以说明新卷已就绪。 */
static BOOL refresh_dev_info(wchar_t drive, DEVINFO* out)
{
    for (int attempt = 0; attempt < 4; attempt++) {   /* 1 次 + 重试 3 次 */
        if (attempt > 0) Sleep(200);
        if (dev_get_info(drive, out) && out->has_serial) return TRUE;
    }
    return FALSE;
}

int op_format(const CFGMODEL* m, wchar_t drive, HWND hwnd, const DEVSIG* expect)
{
    const wchar_t* OP = L"FORMAT";
    /* 配置正常屏蔽：按用户判定政策仍属“成功类别”，但单独计数 */
    if (!cfg_enabled(m, L"ENABLE_FORMAT")) {
        applog(CH_ENGINE, LOG_INFO,
               L"[%s] 按配置禁用格式化（ENABLE_FORMAT=0），跳过（按配置正常屏蔽）", OP);
        return OP_SKIP_CFG;
    }

    /* 0) 配置校验：任何一项不合法都不得弹确认框、不得调用 FormatEx */
    wchar_t fs[32], volume[64];
    cfg_get_def(m, L"FILESYSTEM", L"FAT32", fs, 32);
    cfg_get_def(m, L"VOLUME", L"", volume, 64);

    if (_wcsicmp(fs, L"FAT32") != 0 && _wcsicmp(fs, L"EXFAT") != 0) {
        fail(OP, L"FILESYSTEM=%s 不受支持（仅支持 FAT32/EXFAT），已取消格式化", fs);
        return OP_FAIL;
    }
    if (volume[0]) {
        size_t vlen = wcslen(volume);
        if (vlen > 11) {
            fail(OP, L"VOLUME 卷标过长（%u 个字符，FAT32/EXFAT 卷标最多 11 个字符）：%s",
                 (unsigned)vlen, volume);
            return OP_FAIL;
        }
        for (size_t i = 0; i < vlen; i++) {
            wchar_t c = volume[i];
            if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
                c == L'"' || c == L'<' || c == L'>' || c == L'|') {
                fail(OP, L"VOLUME 卷标包含非法字符 '%c'（禁止 \\ / : * ? \" < > |）：%s",
                     c, volume);
                return OP_FAIL;
            }
            if (c < 0x20) {
                fail(OP, L"VOLUME 卷标包含控制字符 0x%02X：%s", (unsigned)c, volume);
                return OP_FAIL;
            }
        }
    }

    /* CONFIRM 仍读取（仅用于兼容/日志）：不再允许 CONFIRM=0 跳过确认，格式化必须人工确认 */
    int confirm = cfg_get_int(m, L"CONFIRM", 1);
    if (!confirm)
        applog(CH_ENGINE, LOG_WARN,
               L"[%s] 配置 CONFIRM=0：格式化仍强制人工确认（安全要求，CONFIRM 不再能跳过确认）", OP);

    /* 1) 身份基线：任务基线 expect 优先，否则现场采集 */
    DEVSIG base;
    memset(&base, 0, sizeof(base));
    if (expect && expect->drive) {
        base = *expect;
    } else if (!dev_capture_sig(drive, &base)) {
        fail(OP, L"无法采集 %c: 盘的设备身份信息（盘可能未就绪），已取消格式化", drive);
        return OP_FAIL;
    }

    /* 当前盘信息（卷标/容量/文件系统）供确认文案显示 */
    DEVINFO di;
    dev_get_info(drive, &di);
    wchar_t sig[320] = L"";
    dev_sig_describe(&base, sig, 320);

    /* 2) 强制人工确认（无论 CONFIRM 配置为何值都必须确认） */
    wchar_t msg[900];
    _snwprintf(msg, 900,
               L"确认格式化 %c: 盘？\n\n"
               L"卷标：%s\n"
               L"容量：%.2f GB\n"
               L"文件系统：%s\n"
               L"%s\n\n"
               L"该盘所有数据将被清除，且不可恢复！\n\n"
               L"确定继续格式化吗？",
               drive,
               di.volume[0] ? di.volume : L"(无卷标)",
               di.total_bytes / 1073741824.0,
               di.fs[0] ? di.fs : L"未知",
               sig);
    msg[899] = 0;
    if (MessageBoxW(hwnd, msg, L"格式化确认（危险操作）",
                    MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK) {
        applog(CH_ENGINE, LOG_INFO, L"[%s] 操作员取消格式化", OP);
        return OP_STOPPED;
    }

    /* 3) 确认后再次复核身份：不一致绝不调用 FormatEx */
    {
        DEVSIG cur;
        BOOL cap = dev_capture_sig(drive, &cur);
        if (!cap || !dev_sig_same_device(&base, &cur)) {
            wchar_t a[320] = L"", b[320] = L"";
            dev_sig_describe(&base, a, 320);
            if (cap) dev_sig_describe(&cur, b, 320);
            else     lstrcpynW(b, L"无法读取（盘可能已拔出）", 320);
            fail(OP, L"确认后目标盘已变化（原 %s / 现 %s），已取消格式化，未执行 FormatEx", a, b);
            return OP_FAIL;
        }
    }

    /* 开始真正的格式化：进度条先复位 */
    post_progress(0);
    applog(CH_ENGINE, LOG_INFO, L"[%s] 开始格式化 %c: (%s, 快速)", OP, drive, fs);

    HMODULE fm = LoadLibraryW(L"fmifs.dll");
    if (!fm) { fail(OP, L"加载 fmifs.dll 失败"); return OP_FAIL; }
    FN_FormatEx fmt = (FN_FormatEx)GetProcAddress(fm, "FormatEx");
    if (!fmt) { fail(OP, L"找不到 FormatEx 入口"); FreeLibrary(fm); return OP_FAIL; }

    wchar_t ntdev[16];
    _snwprintf(ntdev, 16, L"\\\\.\\%c:", drive);
    wchar_t label[64];
    lstrcpynW(label, volume, 64);

    /* 4) 调用 FormatEx 前最后检查停止标志（不对已停止的任务执行破坏性操作） */
    if (CHECK_STOP()) {
        applog(CH_ENGINE, LOG_WARN, L"[%s] 已停止，取消格式化（未执行 FormatEx）", OP);
        FreeLibrary(fm);
        return OP_STOPPED;
    }

    BOOLEAN ok = fmt(ntdev, 0x0C /* FMIFS_HARDDISK */, fs, label, TRUE, 0, fmifs_cb);
    FreeLibrary(fm);
    post_progress(100);

    if (!ok) {
        DWORD err = GetLastError();
        if (dev_lost_error(err))
            fail(OP, L"格式化 %c: 失败：设备已移除/未就绪（错误码 %lu）", drive, err);
        else
            fail(OP, L"格式化 %c: 失败（错误码 %lu；如无权限请以管理员身份重试）", drive, err);
        return OP_FAIL;
    }

    applog(CH_ENGINE, LOG_INFO, L"[%s] FormatEx 返回成功 %c:，正在写入卷标并刷新设备信息", OP, drive);

    /* 格式化后写卷标：失败必须判失败（格式化已完成但卷标未生效） */
    if (volume[0]) {
        wchar_t root[8];
        _snwprintf(root, 8, L"%c:\\", drive);
        if (!SetVolumeLabelW(root, volume)) {
            DWORD err = GetLastError();
            fail(OP, L"格式化已完成，但写入卷标失败（错误码 %lu）：%s（卷标可能未生效，请手动设置）",
                 err, volume);
            return OP_FAIL;
        }
        applog(CH_ENGINE, LOG_OK, L"[%s] 卷标已写入：%s", OP, volume);
    }

    /* 刷新设备信息：卷序列号/文件系统/容量在格式化后都会变化 */
    DEVINFO ndi;
    memset(&ndi, 0, sizeof(ndi));
    if (refresh_dev_info(drive, &ndi)) {
        wchar_t txt[512];
        build_label_text(drive, &ndi, txt, 512);
        applog(CH_ENGINE, LOG_OK,
               L"[%s] 格式化 %c: 完成：卷标=%s 文件系统=%s 容量=%.2fGB",
               OP, drive,
               ndi.volume[0] ? ndi.volume : L"(无卷标)",
               ndi.fs[0] ? ndi.fs : fs,
               ndi.total_bytes / 1073741824.0);
        post_label(txt);
    } else {
        /* 格式化本身已成功，仅信息刷新失败：只 WARN，不把成功改成失败 */
        applog(CH_ENGINE, LOG_WARN,
               L"[%s] 格式化 %c: 已完成，但刷新设备信息失败（盘可能未就绪）：卷标=%s 文件系统=%s",
               OP, drive, volume[0] ? volume : L"(无卷标)", fs);
    }
    /* 请求 GUI 重新采集并刷新设备/标签行（即使本地刷新失败也尝试一次） */
    post_devrefresh();
    return OP_OK;
}

/* ============================================================
 * usbinfo - USB 通讯模式识别（USB 2.0 / 3.x，取不到标注未知）
 *
 * 旧实现用 IOCTL_STORAGE_QUERY_PROPERTY + 自造 PROTO_DATA 查询
 * “协议规格”，属性 ID（51 实为温度属性）、协议类型（2=ATA）与结构
 * 布局均错误，已整体删除。现改为 usbspeed.c 的 USB Hub IOCTL 匹配：
 * 通过设备所连 Hub 端口的协商速度判定，取不到只 WARN，不判失败。
 * ============================================================ */

int op_usbinfo(const CFGMODEL* m, wchar_t drive)
{
    const wchar_t* OP = L"USBINFO";
    /* 配置正常屏蔽：按用户判定政策仍属“成功类别”，但单独计数 */
    if (!cfg_enabled(m, L"ENABLE_USBINFO")) {
        applog(CH_ENGINE, LOG_INFO,
               L"[%s] 按配置禁用 USB 识别（ENABLE_USBINFO=0），跳过（按配置正常屏蔽）", OP);
        return OP_SKIP_CFG;
    }

    /* 盘根存在性：已移除/无效盘符直接失败（不猜测、不查 Hub） */
    wchar_t root[8];
    _snwprintf(root, 8, L"%c:\\", drive);
    root[7] = 0;
    UINT dt = GetDriveTypeW(root);
    if (dt == DRIVE_NO_ROOT_DIR || dt == DRIVE_UNKNOWN) {
        fail(OP, L"目标盘 %c: 不可访问/已移除，无法识别 USB 通讯模式", drive);
        return OP_FAIL;
    }

    DEVINFO di;
    memset(&di, 0, sizeof(di));
    if (!dev_get_info(drive, &di))
        applog(CH_ENGINE, LOG_WARN, L"[%s] 读取 %c: 设备信息失败（仍尝试 USB 识别）", OP, drive);
    applog(CH_ENGINE, LOG_INFO, L"[%s] %c: 卷标=%s 文件系统=%s 容量=%.1fGB",
           OP, drive, di.volume[0] ? di.volume : L"(无)", di.fs[0] ? di.fs : L"未知",
           di.total_bytes / (1024.0 * 1024 * 1024));
    if (di.has_serial)
        applog(CH_ENGINE, LOG_INFO, L"[%s] 卷序列号=%08lX", OP, (unsigned long)di.serial);

    /* USB Hub IOCTL 匹配 VID/PID → 端口协商速度 */
    USBSpeed us;
    memset(&us, 0, sizeof(us));
    if (usb_query_speed(&di, &us)) {
        const wchar_t* name = usb_speed_name(us.speed);
        applog(CH_ENGINE, LOG_OK, L"[%s] 通讯模式：%s（%s）",
               OP, name ? name : L"未知", us.detail);
    } else {
        /* 取不到不判错误：现场仍可正常使用，只是未知 */
        applog(CH_ENGINE, LOG_WARN, L"[%s] 通讯模式：未知（%s）", OP,
               us.detail[0] ? us.detail : L"采集失败，原因未知");
    }
    applog(CH_ENGINE, LOG_INFO, L"[%s] Type-C 正反插识别：预留（需 USB 事件监听扩展）", OP);
    return OP_OK;
}

/* ============================================================
 * 标签读写
 * ============================================================ */

int op_label_read(const CFGMODEL* m, wchar_t drive)
{
    (void)m;
    /* 盘根存在性：已移除/无效盘符 */
    wchar_t root[8];
    _snwprintf(root, 8, L"%c:\\", drive);
    root[7] = 0;
    UINT dt = GetDriveTypeW(root);
    if (dt == DRIVE_NO_ROOT_DIR || dt == DRIVE_UNKNOWN) {
        fail(L"LABEL", L"目标盘 %c: 已移除/不可访问，无法读取标签", drive);
        return OP_FAIL;
    }

    DEVINFO di;
    memset(&di, 0, sizeof(di));
    if (!dev_get_info(drive, &di)) {
        fail(L"LABEL", L"读取 %c: 标签失败（盘未就绪或无法访问）", drive);
        return OP_FAIL;
    }
    /* 卷信息不可用（未格式化/未就绪/刚插入）时不得输出看似正常的标签 */
    if (!di.has_serial || !di.fs[0] || _wcsicmp(di.fs, L"未知") == 0) {
        fail(L"LABEL", L"无法读取卷信息（盘未格式化或未就绪）：%c:", drive);
        return OP_FAIL;
    }

    wchar_t txt[512];
    build_label_text(drive, &di, txt, 512);
    applog(CH_ENGINE, LOG_OK, L"[LABEL] %s", txt);
    post_label(txt);
    return OP_OK;
}

/* ============================================================
 * rwtest - 读写测试（写 8MB 测试文件 → 回读 → MD5 比对 → 速率）
 *
 * 防“缓存假阳性”：测试文件打开时加 FILE_FLAG_NO_BUFFERING |
 * FILE_FLAG_WRITE_THROUGH，缓冲按扇区对齐、读写按 64KB 扇区对齐，
 * 强制真正落到介质；打开失败才回退缓冲模式并 WARN（结果可能受缓存影响）。
 * ============================================================ */

#define RWTEST_SIZE   (8 * 1024 * 1024)   /* 8MB，向上取整到扇区 */
#define RWTEST_CHUNK  (64 * 1024)         /* 读写块 64KB（64KB 为扇区整数倍） */

/* 删除读写测试文件；删除失败只 WARN（清理失败不改变测试结论） */
static void rwtest_delete_file(const wchar_t* op, const wchar_t* path)
{
    if (DeleteFileW(path)) return;
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND) return;
    applog(CH_ENGINE, LOG_WARN, L"[%s] 无法删除测试文件 %s（错误码 %lu）", op, path, err);
}

/* 取扇区大小：失败或非 2 的幂时回退 512（NO_BUFFERING 要求扇区对齐） */
static DWORD rwtest_sector_size(const wchar_t* root)
{
    DWORD spc = 0, bps = 0, freec = 0, totalc = 0;
    if (GetDiskFreeSpaceW(root, &spc, &bps, &freec, &totalc) &&
        bps != 0 && (bps & (bps - 1)) == 0 && bps <= (DWORD)RWTEST_CHUNK)
        return bps;
    return 512;
}

int op_rwtest(const CFGMODEL* m, wchar_t drive, const DEVSIG* expect)
{
    const wchar_t* OP = L"RWTEST";
    (void)m;
    /* 进入复核：任务基线（expect）与当前设备不一致则不做任何读写 */
    if (expect && expect->drive && check_expect(OP, drive, expect)) return OP_FAIL;

    DEVSIG base;
    BOOL have_base = FALSE;
    if (expect && expect->drive) {
        base = *expect;
        have_base = TRUE;
    } else if (dev_capture_sig(drive, &base)) {
        have_base = TRUE;
    }

    int rc = OP_FAIL;
    DWORD sector = 512;
    DWORD size = 0;
    unsigned char* wbase = NULL;
    unsigned char* rbase = NULL;
    unsigned char* wbuf = NULL;
    unsigned char* rbuf = NULL;
    HANDLE h = INVALID_HANDLE_VALUE;
    wchar_t path[MAX_PATH];
    path[0] = 0;

    wchar_t root[8];
    _snwprintf(root, 8, L"%c:\\", drive);
    root[7] = 0;
    sector = rwtest_sector_size(root);
    size = (RWTEST_SIZE + sector - 1) & ~(sector - 1);   /* 8MB 向上取整到扇区 */

    applog(CH_ENGINE, LOG_INFO,
           L"[%s] %c: 开始读写测试（%.1fMB，扇区 %lu 字节，写→读→MD5 比对）",
           OP, drive, size / 1048576.0, (unsigned long)sector);

    /* 缓冲：VirtualAlloc 页对齐 + 额外一个扇区保证按扇区对齐 */
    wbase = (unsigned char*)VirtualAlloc(NULL, size + sector,
                                         MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    rbase = (unsigned char*)VirtualAlloc(NULL, size + sector,
                                         MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!wbase || !rbase) {
        fail(OP, L"无法分配读写测试缓冲（VirtualAlloc 失败，错误码 %lu）", GetLastError());
        goto done;
    }
    wbuf = (unsigned char*)(((ULONG_PTR)wbase + sector - 1) & ~((ULONG_PTR)sector - 1));
    rbuf = (unsigned char*)(((ULONG_PTR)rbase + sector - 1) & ~((ULONG_PTR)sector - 1));

    /* 关键点复核 1/2：开始写之前 */
    if (have_base && check_expect(OP, drive, &base)) goto done;

    srand((unsigned)time(NULL) ^ (unsigned)drive);
    for (DWORD i = 0; i < size; i++) wbuf[i] = (unsigned char)(rand() & 0xFF);
    unsigned char wdigest[16], rdigest[16];
    {
        md5_context c;
        md5_init(&c);
        md5_update(&c, wbuf, size);
        md5_final(&c, wdigest);
    }

    /* 测试文件名带 PID，避免并发/残留互相覆盖 */
    _snwprintf(path, MAX_PATH, L"%c:\\udk_rwtest_%lu.bin",
               drive, (unsigned long)GetCurrentProcessId());
    path[MAX_PATH - 1] = 0;

    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceFrequency(&freq);

    /* ---------- 写：优先 NO_BUFFERING + WRITE_THROUGH（直写介质） ---------- */
    BOOL write_direct = TRUE;
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                    NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        write_direct = FALSE;
        applog(CH_ENGINE, LOG_WARN,
               L"[%s] 无法以 NO_BUFFERING/WRITE_THROUGH 打开测试文件（错误码 %lu），"
               L"回退缓冲模式（结果可能受系统缓存影响）", OP, err);
        h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            fail_io(OP, L"无法创建测试文件", GetLastError());
            goto done;
        }
    }
    QueryPerformanceCounter(&t0);
    DWORD wr = 0, werr = 0;
    BOOL ok = TRUE;
    for (DWORD off = 0; off < size; off += RWTEST_CHUNK) {
        if (CHECK_STOP()) { ok = FALSE; break; }
        DWORD w = 0;
        if (!WriteFile(h, wbuf + off, RWTEST_CHUNK, &w, NULL)) {
            werr = GetLastError();
            ok = FALSE;
            break;
        }
        wr += w;
    }
    FlushFileBuffers(h);
    QueryPerformanceCounter(&t1);
    double wsec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    CloseHandle(h);
    h = INVALID_HANDLE_VALUE;
    if (!ok) {
        if (CHECK_STOP()) {
            applog(CH_ENGINE, LOG_WARN, L"[%s] 已停止（写入中断）", OP);
            rc = OP_STOPPED;
            goto done;
        }
        fail_io(OP, L"写入中断", werr ? werr : GetLastError());
        goto done;
    }
    double wmbps = wr / 1048576.0 / (wsec > 0 ? wsec : 0.001);
    applog(CH_ENGINE, LOG_INFO, L"[%s] 写入 %.2f MB/s（%s）", OP, wmbps,
           write_direct ? L"NO_BUFFERING/WRITE_THROUGH 直读介质，未走缓存"
                        : L"缓冲模式，结果可能受系统缓存影响");

    /* 关键点复核 2/2：开始回读之前 */
    if (have_base && check_expect(OP, drive, &base)) goto done;

    /* ---------- 读：优先 NO_BUFFERING（直接读介质） ---------- */
    BOOL read_direct = TRUE;
    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                    FILE_FLAG_NO_BUFFERING, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        read_direct = FALSE;
        applog(CH_ENGINE, LOG_WARN,
               L"[%s] 无法以 NO_BUFFERING 回读测试文件（错误码 %lu），"
               L"回退缓冲模式（结果可能受系统缓存影响）", OP, err);
        h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            fail_io(OP, L"无法回读测试文件", GetLastError());
            goto done;
        }
    }
    QueryPerformanceCounter(&t0);
    DWORD rr = 0, rerr = 0;
    ok = TRUE;
    for (DWORD off = 0; off < size; off += RWTEST_CHUNK) {
        if (CHECK_STOP()) { ok = FALSE; break; }
        DWORD r = 0;
        if (!ReadFile(h, rbuf + off, RWTEST_CHUNK, &r, NULL)) {
            rerr = GetLastError();
            ok = FALSE;
            break;
        }
        rr += r;
    }
    QueryPerformanceCounter(&t1);
    double rsec = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;
    CloseHandle(h);
    h = INVALID_HANDLE_VALUE;
    if (!ok || rr != wr) {
        if (CHECK_STOP()) {
            applog(CH_ENGINE, LOG_WARN, L"[%s] 已停止（回读中断）", OP);
            rc = OP_STOPPED;
            goto done;
        }
        if (!ok)
            fail_io(OP, L"回读中断", rerr ? rerr : GetLastError());
        else
            fail(OP, L"回读字节数不符（%lu/%lu）", (unsigned long)rr, (unsigned long)wr);
        goto done;
    }
    double rmbps = rr / 1048576.0 / (rsec > 0 ? rsec : 0.001);
    applog(CH_ENGINE, LOG_INFO, L"[%s] 读取 %.2f MB/s（%s）", OP, rmbps,
           read_direct ? L"NO_BUFFERING 直读介质，未走缓存"
                       : L"缓冲模式，结果可能受系统缓存影响");

    {
        md5_context c;
        md5_init(&c);
        md5_update(&c, rbuf, rr);
        md5_final(&c, rdigest);
    }
    BOOL same = memcmp(wdigest, rdigest, 16) == 0;
    if (same) {
        applog(CH_ENGINE, LOG_OK, L"[%s] 读写比对一致，测试通过", OP);
        post_progress(100);
        rc = OP_OK;
    } else {
        fail(OP, L"读写比对不一致，U 盘可能存在坏块！");
    }

done:
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    if (path[0]) rwtest_delete_file(OP, path);
    if (wbase) VirtualFree(wbase, 0, MEM_RELEASE);
    if (rbase) VirtualFree(rbase, 0, MEM_RELEASE);
    return rc;
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
    return OP_OK;
}
