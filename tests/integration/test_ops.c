/* ============================================================
 * test_ops.c - 操作引擎集成测试
 *   op_write 结果码/目标目录创建时机（P3-3）、op_read 判定与 ${MATCH_1} 截断（P2-16）、
 *   配置正常屏蔽 / 程序自行降级的结果码与 GUI 消息语义。
 * 来源：第 2/6 批验收用例合并；不依赖 U 盘：目标盘取 %TEMP% 所在固定盘，
 * 无匹配盘符/设备时相关断言优雅 SKIP。所有写盘仅在 %TEMP% 自建目录并清理。
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ops.h"
#include "config.h"
#include "device.h"
#include "pattern.h"
#include "util.h"
#include "gui.h"      /* WM_APP_VERDICT/FILECONTENT/ERROR */

static int pass = 0, fail = 0, skip = 0;

static void ok(BOOL cond, const char* name)
{
    if (cond) { pass++; printf("PASS  %s\n", name); }
    else      { fail++; printf("FAIL  %s\n", name); }
}
static void skipmsg(const char* why)
{
    skip++;
    printf("SKIP  %s\n", why);
}
static void check_rc(const char* name, int got, int want)
{
    if (got == want) { pass++; printf("PASS  %-58s rc=%d\n", name, got); }
    else { fail++; printf("FAIL  %-58s rc=%d want=%d\n", name, got, want); }
}

/* ---------------- 临时目录（%TEMP%，可清理） ---------------- */
static wchar_t TDIR[MAX_PATH];       /* 不含结尾反斜杠 */
static wchar_t DREL[MAX_PATH];       /* TDIR 去掉盘符后的相对路径（含盘根反斜杠） */
static wchar_t SRC[MAX_PATH];
static wchar_t DRIVE = L'C';

static void remove_tree(const wchar_t* path)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    path_join(pat, MAX_PATH, path, L"*");
    h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t full[MAX_PATH];
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            path_join(full, MAX_PATH, path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(full);
            else { SetFileAttributesW(full, FILE_ATTRIBUTE_NORMAL); DeleteFileW(full); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(path);
}

static void init_temp(void)
{
    wchar_t tmp[MAX_PATH] = L"";
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) tmp[0] = 0;
    _snwprintf(TDIR, MAX_PATH, L"%sudk_tests_ops_%lu", tmp,
               (unsigned long)GetCurrentProcessId());
    TDIR[MAX_PATH - 1] = 0;
    DRIVE = TDIR[0];
    lstrcpynW(DREL, TDIR + 2, MAX_PATH);          /* "Users\\...\\udk_tests_ops_<pid>" */
    CreateDirectoryW(TDIR, NULL);
    path_join(SRC, MAX_PATH, TDIR, L"src");
    CreateDirectoryW(SRC, NULL);
}

static BOOL mkfile(const wchar_t* path, const char* text)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w = 0, n = (DWORD)strlen(text);
    BOOL okk;
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    okk = WriteFile(h, text, n, &w, NULL) && w == n;
    CloseHandle(h);
    return okk;
}

static BOOL file_exists(const wchar_t* path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static BOOL dir_exists(const wchar_t* path)
{
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
static BOOL missing(const wchar_t* path)
{
    return GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES;
}

/* 构造 DSTPATH（相对盘根，不含盘符），例如 "\Users\...\dst\tail" */
static void make_dstrel(wchar_t* out, size_t cap, const wchar_t* tail)
{
    _snwprintf(out, cap, L"\\%s\\dst\\%s", DREL, tail);
    out[cap - 1] = 0;
}

/* ---------------- CFGMODEL ---------------- */
static CFGMODEL g_model;
static void model_reset(void)
{
    memset(&g_model, 0, sizeof(g_model));
    lstrcpynW(g_model.internal, L"TEST", CFG_KEY_LEN);
    lstrcpynW(g_model.display, L"TEST", CFG_VAL_LEN);
}
static void model_set(const wchar_t* k, const wchar_t* v)
{
    if (g_model.nkeys >= CFG_MAX_KEYS) return;
    lstrcpynW(g_model.keys[g_model.nkeys].key, k, CFG_KEY_LEN);
    lstrcpynW(g_model.keys[g_model.nkeys].val, v, CFG_VAL_LEN);
    g_model.nkeys++;
}

/* ---------------- 隐藏窗口：接收 ops → GUI 消息 ---------------- */
typedef struct { int verdict_pass, verdict_fail, verdict_other, filecontent, error; } MSGS;

static LRESULT CALLBACK wnd_proc_test(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)wp; (void)lp;
    return DefWindowProcW(h, msg, wp, lp);
}
static HWND make_hidden(void)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc_test;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"UDK_TEST_OPS_CLASS";
    RegisterClassW(&wc);
    return CreateWindowExW(0, L"UDK_TEST_OPS_CLASS", L"", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
}
static void drain_msgs(HWND h, MSGS* out)
{
    MSG msg;
    memset(out, 0, sizeof(*out));
    while (PeekMessageW(&msg, h, 0, 0, PM_REMOVE)) {
        switch (msg.message) {
        case WM_APP_VERDICT: {
            wchar_t* s = (wchar_t*)msg.lParam;
            if (s) {
                if (wcscmp(s, L"PASS") == 0) out->verdict_pass++;
                else if (wcscmp(s, L"FAIL") == 0) out->verdict_fail++;
                else out->verdict_other++;
                free(s);
            }
            break;
        }
        case WM_APP_FILECONTENT:
            free((void*)msg.lParam);
            out->filecontent++;
            break;
        case WM_APP_ERROR:
            free((void*)msg.lParam);
            out->error++;
            break;
        default:
            break;
        }
    }
}

/* 日志辅助（op_read 截断 WARN） */
static void clear_logs(void)
{
    wchar_t dir[MAX_PATH];
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    log_shutdown();
    path_join(dir, MAX_PATH, exe_dir(), L"logs");
    path_join(pat, MAX_PATH, dir, L"*.log*");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wchar_t full[MAX_PATH];
        path_join(full, MAX_PATH, dir, fd.cFileName);
        DeleteFileW(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static BOOL log_contains(const wchar_t* needle)
{
    wchar_t dir[MAX_PATH], pat[MAX_PATH], full[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wchar_t* text = NULL;
    BOOL found = FALSE;

    path_join(dir, MAX_PATH, exe_dir(), L"logs");
    path_join(pat, MAX_PATH, dir, L"*.log");
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    path_join(full, MAX_PATH, dir, fd.cFileName);
    FindClose(h);
    if (read_file_text(full, &text) && text) {
        found = wcsstr(text, needle) != NULL;
        free(text);
    }
    return found;
}

/* 构造 READFILE（相对盘根路径） */
static void make_readfile(wchar_t* out, size_t cap, const wchar_t* tail)
{
    _snwprintf(out, cap, L"%s\\%s", DREL, tail);
    out[cap - 1] = 0;
}

int main(void)
{
    wchar_t srcpat[MAX_PATH];
    wchar_t dstrel[CFG_VAL_LEN];
    wchar_t target[MAX_PATH];
    wchar_t readdir[MAX_PATH];
    wchar_t path2[MAX_PATH];
    HWND hw;

    init_temp();
    path_join(srcpat, MAX_PATH, SRC, L"*.*");
    path_join(readdir, MAX_PATH, TDIR, L"read");
    CreateDirectoryW(readdir, NULL);
    printf("== test_ops: drive=%c temp=%ls ==\n", DRIVE, TDIR);

    /* ================= 1. 结果码定义 ================= */
    check_rc("OP_OK value", OP_OK, 0);
    check_rc("OP_FAIL value", OP_FAIL, 1);
    check_rc("OP_STOPPED value", OP_STOPPED, 2);
    check_rc("OP_SKIP_CFG value", OP_SKIP_CFG, 3);

    /* ================= 2. pattern 捕获截断（P2-16） ================= */
    {
        wchar_t cap[8];
        BOOL tr = FALSE;
        const wchar_t* p20 = L"GTB907785????????????????????";
        const wchar_t* n20 = L"GTB907785ABCDEFGHIJKLMNOPQRST";
        ok(pat_match_capture_ex(p20, n20, cap, 8, &tr) == TRUE, "pattern: match TRUE with small cap");
        ok(tr == TRUE, "pattern: truncated flag TRUE");
        ok(wcscmp(cap, L"ABCDEFG") == 0, "pattern: cap keeps NUL-terminated prefix");
        tr = TRUE;
        {
            wchar_t big[32];
            ok(pat_match_capture_ex(p20, n20, big, 32, &tr) == TRUE && tr == FALSE,
               "pattern: big cap not truncated");
            ok(wcscmp(big, L"ABCDEFGHIJKLMNOPQRST") == 0, "pattern: big cap full capture");
        }
        ok(pat_match_capture_ex(p20, n20, NULL, 0, &tr) == TRUE, "pattern: NULL cap safe");
        ok(pat_match_capture_ex(NULL, n20, cap, 8, &tr) == FALSE, "pattern: NULL pattern FALSE");
        ok(pat_match_capture(p20, n20, cap, 8) == TRUE, "pattern: legacy API still works");
    }

    /* ================= 3. op_write 配置/结果码 ================= */
    model_reset();
    model_set(L"ENABLE_WRITE", L"0");
    check_rc("op_write ENABLE_WRITE=0 -> SKIP_CFG", op_write(&g_model, DRIVE, NULL), OP_SKIP_CFG);

    model_reset();
    model_set(L"ENABLE_WRITE", L"1");
    model_set(L"WRITEMODE", L"PROCESSED");
    model_set(L"SRCPATH", srcpat);
    check_rc("op_write WRITEMODE=PROCESSED -> FAIL", op_write(&g_model, DRIVE, NULL), OP_FAIL);

    model_reset();
    model_set(L"ENABLE_WRITE", L"1");
    check_rc("op_write no SRCPATH -> FAIL", op_write(&g_model, DRIVE, NULL), OP_FAIL);

    model_reset();
    model_set(L"ENABLE_WRITE", L"1");
    model_set(L"HASH", L"sha256");
    model_set(L"SRCPATH", srcpat);
    check_rc("op_write HASH=sha256 -> FAIL", op_write(&g_model, DRIVE, NULL), OP_FAIL);

    /* ================= 4. op_write P3-3：先匹配后建目录 ================= */
    /* 4a 源目录为空 → FAIL 且目标目录不创建 */
    make_dstrel(dstrel, CFG_VAL_LEN, L"empty");
    model_reset();
    model_set(L"ENABLE_WRITE", L"1");
    model_set(L"WRITEMODE", L"COPY");
    model_set(L"HASH", L"md5");
    model_set(L"SRCPATH", srcpat);
    model_set(L"DSTPATH", dstrel);
    path_join(target, MAX_PATH, TDIR, L"dst");
    path_join(path2, MAX_PATH, target, L"empty");
    remove_tree(path2);
    check_rc("op_write empty src -> FAIL", op_write(&g_model, DRIVE, NULL), OP_FAIL);
    ok(missing(path2), "op_write empty src: target dir NOT created");

    /* 4b 源目录存在但无匹配文件（*.bin vs .txt）→ FAIL 且不建目录 */
    {
        wchar_t f[MAX_PATH];
        path_join(f, MAX_PATH, SRC, L"only.txt");
        mkfile(f, "txt");
        model_reset();
        model_set(L"ENABLE_WRITE", L"1");
        model_set(L"WRITEMODE", L"COPY");
        model_set(L"HASH", L"md5");
        {
            wchar_t pat[MAX_PATH];
            path_join(pat, MAX_PATH, SRC, L"*.bin");
            model_set(L"SRCPATH", pat);
        }
        make_dstrel(dstrel, CFG_VAL_LEN, L"nomatch");
        model_set(L"DSTPATH", dstrel);
        path_join(path2, MAX_PATH, target, L"nomatch");
        remove_tree(path2);
        check_rc("op_write no wildcard match -> FAIL", op_write(&g_model, DRIVE, NULL), OP_FAIL);
        ok(missing(path2), "op_write no match: target dir NOT created");
    }

    /* 4c 源目录不存在 → FAIL 且不建目录 */
    {
        wchar_t pat[MAX_PATH];
        path_join(pat, MAX_PATH, TDIR, L"nosuch\\*.*");
        model_reset();
        model_set(L"ENABLE_WRITE", L"1");
        model_set(L"WRITEMODE", L"COPY");
        model_set(L"HASH", L"md5");
        model_set(L"SRCPATH", pat);
        make_dstrel(dstrel, CFG_VAL_LEN, L"missingsrc");
        model_set(L"DSTPATH", dstrel);
        path_join(path2, MAX_PATH, target, L"missingsrc");
        remove_tree(path2);
        check_rc("op_write missing src dir -> FAIL", op_write(&g_model, DRIVE, NULL), OP_FAIL);
        ok(missing(path2), "op_write missing src: target dir NOT created");
    }

    /* 4d 有匹配文件 → OK，目标目录创建，内容一致（含 MD5 校验路径） */
    {
        wchar_t f[MAX_PATH];
        wchar_t dstf[MAX_PATH];
        path_join(f, MAX_PATH, SRC, L"a.bin");
        mkfile(f, "batch6-copy-content");
        model_reset();
        model_set(L"ENABLE_WRITE", L"1");
        model_set(L"WRITEMODE", L"COPY");
        model_set(L"HASH", L"md5");
        model_set(L"SRCPATH", srcpat);
        make_dstrel(dstrel, CFG_VAL_LEN, L"ok");
        model_set(L"DSTPATH", dstrel);
        path_join(path2, MAX_PATH, target, L"ok");
        remove_tree(path2);
        check_rc("op_write normal copy (md5) -> OK", op_write(&g_model, DRIVE, NULL), OP_OK);
        ok(dir_exists(path2), "op_write created target dir after match");
        path_join(dstf, MAX_PATH, path2, L"a.bin");
        ok(file_exists(dstf), "op_write copied file exists");
        {
            wchar_t* s1 = NULL;
            wchar_t* s2 = NULL;
            BOOL same = read_file_text(dstf, &s1) && s1 && read_file_text(f, &s2) && s2 &&
                        wcscmp(s1, s2) == 0;
            ok(same, "op_write copied content matches source");
            free(s1);
            free(s2);
        }
    }

    /* 4e 操作员停止 → OP_STOPPED */
    model_reset();
    model_set(L"ENABLE_WRITE", L"1");
    model_set(L"WRITEMODE", L"COPY");
    model_set(L"HASH", L"md5");
    model_set(L"SRCPATH", srcpat);
    make_dstrel(dstrel, CFG_VAL_LEN, L"stop");
    model_set(L"DSTPATH", dstrel);
    ops_request_stop();
    check_rc("op_write stop requested -> STOPPED", op_write(&g_model, DRIVE, NULL), OP_STOPPED);
    ops_reset_stop();

    /* 4f 设备身份基线不一致 → FAIL（不创建目标目录） */
    {
        DEVSIG expect;
        memset(&expect, 0, sizeof(expect));
        expect.drive = (DRIVE == L'C') ? L'D' : L'C';
        expect.has_serial = TRUE;
        expect.volume_serial = 0x12345678;
        make_dstrel(dstrel, CFG_VAL_LEN, L"guard");
        model_reset();
        model_set(L"ENABLE_WRITE", L"1");
        model_set(L"WRITEMODE", L"COPY");
        model_set(L"HASH", L"md5");
        model_set(L"SRCPATH", srcpat);
        model_set(L"DSTPATH", dstrel);
        path_join(path2, MAX_PATH, target, L"guard");
        remove_tree(path2);
        check_rc("op_write expect mismatch -> FAIL", op_write(&g_model, DRIVE, &expect), OP_FAIL);
        ok(missing(path2), "op_write expect mismatch: target dir NOT created");
    }

    /* ================= 5. op_read 配置/判定 ================= */
    {
        wchar_t f[MAX_PATH];
        wchar_t rf[CFG_VAL_LEN];
        path_join(f, MAX_PATH, readdir, L"data.txt");
        mkfile(f, "8685ABCDEFGH");
        make_readfile(rf, CFG_VAL_LEN, L"read\\data.txt");

        model_reset();
        model_set(L"ENABLE_READ", L"0");
        check_rc("op_read ENABLE_READ=0 -> SKIP_CFG", op_read(&g_model, DRIVE), OP_SKIP_CFG);

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", L"");
        check_rc("op_read READFILE empty -> SKIP_CFG", op_read(&g_model, DRIVE), OP_SKIP_CFG);

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", rf);
        model_set(L"READPATTERN", L"REGEX");
        check_rc("op_read READPATTERN=REGEX -> FAIL", op_read(&g_model, DRIVE), OP_FAIL);

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", rf);
        model_set(L"READRULE", L"UNKNOWN_TOKEN_X");
        check_rc("op_read unknown READRULE -> FAIL", op_read(&g_model, DRIVE), OP_FAIL);

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", rf);
        model_set(L"READRULE", L"");
        check_rc("op_read empty READRULE -> OK", op_read(&g_model, DRIVE), OP_OK);

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", rf);
        model_set(L"MATCH_POLICY", L"NEWEST");
        model_set(L"READRULE", L"MUST_CONTAIN:8685;MIN_LEN:10");
        check_rc("op_read multi-rule pass -> OK", op_read(&g_model, DRIVE), OP_OK);

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", rf);
        model_set(L"MATCH_POLICY", L"NEWEST");
        model_set(L"READRULE", L"MIN_LEN:99");
        check_rc("op_read multi-rule MIN_LEN fail -> FAIL", op_read(&g_model, DRIVE), OP_FAIL);

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", L"nope.dat");
        check_rc("op_read no matching file -> FAIL", op_read(&g_model, DRIVE), OP_FAIL);
    }

    /* ================= 6. op_read 捕获截断 WARN（P2-16 残余） ================= */
    {
        wchar_t fname[300];
        wchar_t f[MAX_PATH];
        wchar_t pattern_rel[CFG_VAL_LEN];
        size_t n = (size_t)_snwprintf(fname, 300, L"GTB907785");
        int i;

        for (i = 0; i < 130; i++) fname[n + i] = L'x';
        fname[n + 130] = 0;
        wcscat(fname, L".bin");
        path_join(f, MAX_PATH, readdir, fname);
        mkfile(f, "x");

        _snwprintf(pattern_rel, CFG_VAL_LEN, L"%s\\read\\GTB907785", DREL);
        n = wcslen(pattern_rel);
        for (i = 0; i < 130; i++) pattern_rel[n + i] = L'?';
        pattern_rel[n + 130] = 0;
        wcscat(pattern_rel, L".bin");

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", pattern_rel);
        model_set(L"MATCH_POLICY", L"NEWEST");
        model_set(L"READRULE", L"MIN_LEN:1");
        clear_logs();
        check_rc("op_read long capture still OK", op_read(&g_model, DRIVE), OP_OK);
        ok(log_contains(L"可变部分捕获超长已截断（130 字符）"),
           "op_read truncation WARN contains full capture length 130");
        ok(log_contains(L"仅记录前 127 字符"),
           "op_read truncation WARN contains stored prefix length 127");
    }

    /* ================= 7. GUI 消息语义（隐藏窗口） ================= */
    hw = make_hidden();
    if (!hw) {
        skipmsg("cannot create hidden window: skip GUI message assertions");
    } else {
        MSGS m;
        wchar_t f[MAX_PATH];
        wchar_t rf[CFG_VAL_LEN];
        path_join(f, MAX_PATH, readdir, L"data.txt");
        mkfile(f, "8685ABCDEFGH");
        make_readfile(rf, CFG_VAL_LEN, L"read\\data.txt");
        ops_set_result_target(hw);

        model_reset();
        model_set(L"ENABLE_READ", L"0");
        (void)op_read(&g_model, DRIVE);
        drain_msgs(hw, &m);
        ok(m.verdict_pass == 0 && m.verdict_fail == 0 && m.verdict_other == 0 &&
           m.filecontent == 0 && m.error == 0,
           "skip-cfg posts no verdict/filecontent/error");

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", rf);
        model_set(L"READPATTERN", L"REGEX");
        (void)op_read(&g_model, DRIVE);
        drain_msgs(hw, &m);
        ok(m.verdict_fail >= 1 && m.error >= 1 && m.verdict_pass == 0,
           "REGEX posts FAIL verdict + error");

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", rf);
        model_set(L"READRULE", L"UNKNOWN_TOKEN_X");
        (void)op_read(&g_model, DRIVE);
        drain_msgs(hw, &m);
        ok(m.verdict_fail >= 1 && m.verdict_pass == 0,
           "unknown READRULE posts FAIL verdict");

        model_reset();
        model_set(L"ENABLE_READ", L"1");
        model_set(L"READFILE", rf);
        (void)op_read(&g_model, DRIVE);
        drain_msgs(hw, &m);
        ok(m.verdict_pass == 1 && m.filecontent >= 1 && m.verdict_fail == 0,
           "PASS case posts PASS verdict + filecontent");

        ops_set_result_target(NULL);
        DestroyWindow(hw);
    }

    /* ================= 8. 清理 ================= */
    ops_reset_stop();
    log_shutdown();
    remove_tree(TDIR);
    printf("\n[test_ops] PASS=%d FAIL=%d SKIP=%d\n", pass, fail, skip);
    return fail ? 1 : 0;
}
