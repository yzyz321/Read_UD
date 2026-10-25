/* ============================================================
 * test_logging.c - 日志健壮性 / PostMessage 所有权 / 轮询线程 / 卷信息 集成测试
 * 来源：第 5 批验收用例（日志/轮转/alt/post 所有权/轮询/资源），已适配入库：
 *   - 不依赖绝对路径；日志写入 exe 同目录 tests/build/logs（build 目录，gitignored）；
 *   - 资源检查使用同目录 udisktools.exe（run_tests 先构建），缺失时 SKIP；
 *   - 无 U 盘/无某盘符时相关断言 SKIP 而非硬失败。
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "util.h"
#include "device.h"

static int pass = 0, fail = 0, skip = 0;

static void ok(BOOL cond, const wchar_t* what)
{
    if (cond) pass++;
    else { fail++; wprintf(L"FAIL: %ls\n", what); }
}
static void skipmsg(const wchar_t* why)
{
    skip++;
    wprintf(L"SKIP: %ls\n", why);
}

/* ---------------- 隐藏窗口：接收 applog / post_owned_string ---------------- */
static int g_msgs = 0;

static LRESULT CALLBACK hp(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void)w;
    if (m == WM_NCCREATE) return TRUE;
    if (m == WM_APP + 1) {                 /* applog 行（通道|级别 编码在 wparam） */
        wchar_t* s = (wchar_t*)l;
        if (s) { g_msgs++; free(s); }
        return 0;
    }
    if (m == WM_APP + 100) {               /* post_owned_string 测试消息 */
        wchar_t* s = (wchar_t*)l;
        if (s) { wprintf(L"  recv msg=\"%ls\"\n", s); free(s); g_msgs++; }
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static HWND mk_hidden(void)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = hp;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"UDK_TEST_LOGGING";
    RegisterClassW(&wc);
    return CreateWindowExW(0, L"UDK_TEST_LOGGING", L"", 0, 0, 0, 10, 10,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
}

static void drain(HWND h)
{
    MSG m;
    while (PeekMessageW(&m, h, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

/* ---------------- 日志文件辅助 ---------------- */

static void log_dir(wchar_t* out, size_t cap)
{
    path_join(out, cap, exe_dir(), L"logs");
}

static void today_log(wchar_t* out, size_t cap)
{
    SYSTEMTIME st;
    wchar_t dir[MAX_PATH];
    wchar_t name[64];
    GetLocalTime(&st);
    log_dir(dir, MAX_PATH);
    _snwprintf(name, 64, L"%04d%02d%02d.log", st.wYear, st.wMonth, st.wDay);
    name[63] = 0;
    path_join(out, cap, dir, name);
}

static void alt_log(wchar_t* out, size_t cap)
{
    SYSTEMTIME st;
    wchar_t dir[MAX_PATH];
    wchar_t name[64];
    GetLocalTime(&st);
    log_dir(dir, MAX_PATH);
    _snwprintf(name, 64, L"%04d%02d%02d_alt.log", st.wYear, st.wMonth, st.wDay);
    name[63] = 0;
    path_join(out, cap, dir, name);
}

static void delete_logs(void)
{
    wchar_t dir[MAX_PATH];
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    log_shutdown();
    log_dir(dir, MAX_PATH);
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

static BOOL file_has(const wchar_t* path, const char* needle)
{
    HANDLE h = CreateFileW(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz;
    char* b;
    DWORD rd = 0;
    BOOL found;

    if (h == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (64LL * 1024 * 1024)) {
        CloseHandle(h);
        return FALSE;
    }
    b = (char*)malloc((size_t)sz.QuadPart + 1);
    if (!b || !ReadFile(h, b, (DWORD)sz.QuadPart, &rd, NULL)) {
        CloseHandle(h);
        free(b);
        return FALSE;
    }
    CloseHandle(h);
    b[rd] = 0;
    found = strstr(b, needle) != NULL;
    free(b);
    return found;
}

static int count_marker(const wchar_t* path, const char* marker)
{
    HANDLE h = CreateFileW(path, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
    LARGE_INTEGER sz;
    char* b;
    DWORD rd = 0;
    int cnt = 0;
    char* p;

    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (64LL * 1024 * 1024)) {
        CloseHandle(h);
        return 0;
    }
    b = (char*)malloc((size_t)sz.QuadPart + 1);
    if (!b || !ReadFile(h, b, (DWORD)sz.QuadPart, &rd, NULL)) {
        CloseHandle(h);
        free(b);
        return 0;
    }
    CloseHandle(h);
    b[rd] = 0;
    p = b;
    while ((p = strstr(p, marker)) != NULL) { cnt++; p += strlen(marker); }
    free(b);
    return cnt;
}

static DWORD WINAPI log_thread(LPVOID p)
{
    int id = (int)(INT_PTR)p;
    int i;
    for (i = 0; i < 50; i++)
        filelog(LOG_INFO, L"[T%d] TESTLOG-MARK-%d-%02d", id, id, i);
    return 0;
}

/* 取一个本机不存在根目录的盘符；找不到返回 0（调用方 SKIP） */
static wchar_t pick_void_drive(void)
{
    wchar_t d;
    for (d = L'Z'; d >= L'D'; d--) {
        wchar_t root[8];
        _snwprintf(root, 8, L"%c:\\", d);
        if (GetDriveTypeW(root) == DRIVE_NO_ROOT_DIR) return d;
    }
    return 0;
}

/* 系统盘符（用于卷信息检查；本机无任何固定盘时返回 0） */
static wchar_t system_drive(void)
{
    wchar_t v[16] = L"";
    DWORD n = GetEnvironmentVariableW(L"SystemDrive", v, 16);
    if (n > 1 && n < 16 && v[0] && v[1] == L':') return v[0];
    return 0;
}

int main(void)
{
    HWND hw;
    wchar_t tlog[MAX_PATH];
    wchar_t alog[MAX_PATH];
    wchar_t sysdrv;
    wchar_t voiddrive;

    delete_logs();
    hw = mk_hidden();
    if (!hw) { printf("FAIL: create hidden test window\n"); return 1; }

    today_log(tlog, MAX_PATH);
    alt_log(alog, MAX_PATH);

    /* ================= 1. post_owned_string 所有权 ================= */
    ok(post_owned_string(NULL, WM_APP + 100, 0, L"x") == FALSE, L"post NULL hwnd -> FALSE");
    ok(post_owned_string(hw, WM_APP + 100, 0, L"hello-test") == TRUE, L"post valid hwnd -> TRUE");
    drain(hw);
    ok(g_msgs == 1, L"post message received (receiver freed heap string)");
    ok(post_owned_string(hw, WM_APP + 100, 0, NULL) == TRUE, L"post NULL text -> TRUE (empty)");
    drain(hw);
    ok(g_msgs == 2, L"post NULL text received");
    {
        HWND dead = mk_hidden();
        DestroyWindow(dead);
        ok(post_owned_string(dead, WM_APP + 100, 0, L"dead") == FALSE,
           L"post destroyed hwnd -> FALSE (no leak/crash)");
    }

    /* ================= 2. applog 仍投递 GUI ================= */
    log_set_gui_hwnd(hw);
    applog(CH_ENGINE, LOG_OK, L"testlog-applog-msg");
    drain(hw);
    ok(g_msgs == 3, L"applog delivered to GUI window");

    /* ================= 3. 4 线程日志完整 ================= */
    {
        int before = count_marker(tlog, "TESTLOG-MARK-");
        HANDLE th[4];
        int i;
        for (i = 0; i < 4; i++)
            th[i] = CreateThread(NULL, 0, log_thread, (LPVOID)(INT_PTR)i, 0, NULL);
        WaitForMultipleObjects(4, th, TRUE, 10000);
        for (i = 0; i < 4; i++) CloseHandle(th[i]);
        log_shutdown();
        {
            int after = count_marker(tlog, "TESTLOG-MARK-");
            wprintf(L"  log markers before=%d after=%d\n", before, after);
            ok(after - before == 200, L"4 threads x 50 log lines complete (200 markers)");
        }
    }

    /* ================= 4. 主日志被独占 → alt 兜底 ================= */
    {
        HANDLE lock = CreateFileW(tlog, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        ok(lock != INVALID_HANDLE_VALUE, L"lock main log exclusively");
        filelog(LOG_WARN, L"ALT-MARK-TESTLOG");
        log_shutdown();
        if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
        ok(file_has(alog, "ALT-MARK-TESTLOG"), L"alt log fallback written when main locked");
        DeleteFileW(alog);
        filelog(LOG_INFO, L"AFTER-LOCK-MARK-TESTLOG");
        log_shutdown();
        ok(file_has(tlog, "AFTER-LOCK-MARK-TESTLOG"), L"main log resumed after unlock");
        log_shutdown();
        filelog(LOG_INFO, L"AFTER-SHUTDOWN-MARK-TESTLOG");
        log_shutdown();
        ok(file_has(tlog, "AFTER-SHUTDOWN-MARK-TESTLOG"), L"filelog after log_shutdown works");
    }

    /* ================= 5. >8MB 轮转 .log.1（稀疏文件，不占实际空间） ================= */
    {
        wchar_t rot[MAX_PATH];
        HANDLE h;
        LARGE_INTEGER li;
        _snwprintf(rot, MAX_PATH, L"%s.1", tlog);
        rot[MAX_PATH - 1] = 0;
        delete_logs();
        h = CreateFileW(tlog, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        ok(h != INVALID_HANDLE_VALUE, L"create sparse 9MB today log");
        if (h != INVALID_HANDLE_VALUE) {
            li.QuadPart = 9LL * 1024 * 1024;
            SetFilePointerEx(h, li, NULL, FILE_BEGIN);
            SetEndOfFile(h);
            CloseHandle(h);
            filelog(LOG_INFO, L"ROTATE-MARK-TESTLOG");
            log_shutdown();
            {
                LARGE_INTEGER sz;
                HANDLE r = CreateFileW(rot, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                BOOL big = FALSE;
                if (r != INVALID_HANDLE_VALUE) {
                    if (GetFileSizeEx(r, &sz)) big = (sz.QuadPart >= 8LL * 1024 * 1024);
                    CloseHandle(r);
                }
                ok(big, L">8MB log rotated to .log.1");
            }
            ok(file_has(tlog, "ROTATE-MARK-TESTLOG"), L"log continues in fresh file after rotation");
        }
        delete_logs();
    }

    /* ================= 6. 轮询线程 start/stop ================= */
    {
        LARGE_INTEGER f, a, b;
        int k;
        QueryPerformanceFrequency(&f);
        for (k = 0; k < 3; k++) {
            double ms;
            ok(dev_poll_start(hw) == TRUE, L"poll start TRUE");
            Sleep(50);
            QueryPerformanceCounter(&a);
            dev_poll_stop();
            QueryPerformanceCounter(&b);
            ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
            wprintf(L"  poll stop %d: %.1f ms\n", k, ms);
            ok(ms < 3000.0, L"poll stop < 3s (event wake)");
        }
        dev_poll_stop();
        ok(TRUE, L"repeated dev_poll_stop safe");
    }

    /* ================= 7. 卷信息（UI 免 SetupAPI） ================= */
    sysdrv = system_drive();
    if (!sysdrv) {
        skipmsg(L"no fixed system drive: skip volume-info assertions");
    } else {
        DEVINFO dv, di;
        BOOL got = dev_get_info_volume(sysdrv, &dv);
        ok(got == TRUE, L"dev_get_info_volume(system drive) TRUE");
        ok(dv.letter == sysdrv && dv.fs[0] != 0, L"volume info letter/filesystem filled");
        ok(dv.vidpid[0] == 0 && dv.usbserial[0] == 0, L"volume info leaves USB ids empty");
        ok(dev_get_info(sysdrv, &di) == TRUE, L"dev_get_info(system drive) TRUE");
        voiddrive = pick_void_drive();
        if (!voiddrive) {
            skipmsg(L"all drive letters present: skip missing-drive assertions");
        } else {
            DEVINFO mv;
            ok(dev_get_info_volume(voiddrive, &mv) == FALSE, L"dev_get_info_volume(missing drive) FALSE");
            ok(dev_get_info(voiddrive, &mv) == FALSE, L"dev_get_info(missing drive) FALSE");
        }
    }

    /* ================= 8. 资源（图标/manifest/版本），主程序由 run_tests 构建 ================= */
    {
        wchar_t exe[MAX_PATH];
        wchar_t* sl;
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        sl = wcsrchr(exe, L'\\');
        if (sl) sl[1] = 0;
        lstrcatW(exe, L"udisktools.exe");
        if (GetFileAttributesW(exe) == INVALID_FILE_ATTRIBUTES) {
            skipmsg(L"udisktools.exe not built: skip icon/manifest/version resource checks");
        } else {
            HMODULE hm = LoadLibraryExW(exe, NULL, LOAD_LIBRARY_AS_DATAFILE);
            ok(hm != NULL, L"load built udisktools.exe as datafile");
            if (hm) {
                HICON ic = (HICON)LoadImageW(hm, MAKEINTRESOURCEW(101), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);
                ok(ic != NULL, L"icon resource 101 exists");
                if (ic) DestroyIcon(ic);
                {
                    HRSRC mr = FindResourceW(hm, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
                    ok(mr != NULL, L"manifest resource exists");
                    if (mr) {
                        DWORD msz = SizeofResource(hm, mr);
                        HGLOBAL g = LoadResource(hm, mr);
                        char* p = g ? (char*)LockResource(g) : NULL;
                        ok(p && msz > 100 &&
                           strstr(p, "requestedExecutionLevel") != NULL &&
                           strstr(p, "PerMonitorV2") != NULL,
                           L"manifest content (asInvoker/PerMonitorV2)");
                    }
                }
                ok(FindResourceW(hm, MAKEINTRESOURCEW(1), RT_VERSION) != NULL,
                   L"VERSIONINFO resource exists");
                FreeLibrary(hm);
            }
        }
    }

    log_set_gui_hwnd(NULL);
    log_shutdown();
    delete_logs();
    DestroyWindow(hw);
    wprintf(L"\n[test_logging] PASS=%d FAIL=%d SKIP=%d\n", pass, fail, skip);
    return fail ? 1 : 0;
}
