/* test_gui_state.c - GUI 状态层集成测试（标题/进度/复位/设备行/最小尺寸/STATIC 色/热键）
 * 直接 #include gui.c 调用其 static 函数；不启动 GUI 主循环、不碰真实磁盘。
 * run_tests 编译本测试时仅链接 gui.c 之外的 src（gui.c 已由本文件包含）。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include "gui.c"
#include "gui_config.c"   /* gui.c 引用 cfg_register_class/open_config_dialog/cfg_rebuild_model_list */
#include "version.h"   /* APP_VERSION_W：标题中的软件版本（gui.c 也会包含，已有 include guard） */

static int pass = 0, fail = 0;
static void ok(BOOL cond, const wchar_t* what)
{
    if (cond) pass++;
    else { fail++; wprintf(L"FAIL: %ls\n", what); }
}
static HWND mk(const wchar_t* cls, DWORD style, int x, int y, int w, int h)
{
    return CreateWindowExW(0, cls, L"", WS_CHILD | style, x, y, w, h,
                           g_hmain, NULL, GetModuleHandleW(NULL), NULL);
}
int main(void)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    /* 主窗口用一个普通顶层窗口，SetWindowText 可验证 */
    g_hmain = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
                              0, 0, 880, 700, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(g_hmain != NULL, L"dummy main window");
    g_hdev = mk(L"STATIC", SS_LEFT, 0, 0, 200, 20);
    g_hlabel = mk(L"STATIC", SS_LEFT, 0, 0, 200, 20);
    g_hprog = mk(PROGRESS_CLASSW, PBS_SMOOTH, 0, 0, 300, 20);
    g_hprogpct = mk(L"STATIC", SS_RIGHT, 0, 0, 60, 20);
    g_herr = mk(L"STATIC", SS_LEFT, 0, 0, 200, 20);
    g_hverdict = mk(L"STATIC", SS_LEFT, 0, 0, 200, 20);
    g_hfile[0] = mk(L"STATIC", SS_LEFT, 0, 0, 200, 60);
    g_hfile[1] = mk(L"STATIC", SS_LEFT, 0, 0, 200, 60);
    g_hmonfile = mk(L"STATIC", SS_LEFT, 0, 0, 200, 20);
    g_hmonfile2 = mk(L"STATIC", SS_LEFT, 0, 0, 200, 20);
    g_hmonstatus = mk(L"STATIC", SS_LEFT, 0, 0, 200, 20);
    g_riched_ok = FALSE;

    /* ---- 1. 标题同时显示软件版本与配置版本（格式：%s（软件 v%s）（配置 v%s）） ---- */
    {
        wchar_t t[1400] = { 0 };
        wchar_t exp[1400];
        for (int i = 0; i < 500; i++) { g_cfg.title[i] = L'A'; g_cfg.version[i] = L'B'; }
        g_cfg.title[500] = 0; g_cfg.version[500] = 0;
        update_main_title();
        GetWindowTextW(g_hmain, t, 1400);
        _snwprintf(exp, 1400, L"%s（软件 v%s）（配置 v%s）",
                   g_cfg.title, APP_VERSION_W, g_cfg.version);
        exp[1399] = 0;
        wprintf(L"  title len=%d\n", (int)wcslen(t));
        ok(wcscmp(t, exp) == 0, L"title shows software version + config version");
        ok(wcsstr(t, L"（软件 v" APP_VERSION_W L"）") != NULL,
           L"title contains 软件 v1.1.0");
        ok(wcsstr(t, L"（配置 vBBBB") != NULL,
           L"title contains config version");
        ok(wcslen(t) == 500 + (int)wcslen(L"（软件 v" APP_VERSION_W L"）（配置 v") + 500 + 1,
           L"title not truncated");

        /* 配置版本为空：标题只显示软件版本（不带 v 前缀） */
        g_cfg.version[0] = 0;
        update_main_title();
        GetWindowTextW(g_hmain, t, 1400);
        _snwprintf(exp, 1400, L"%s（软件 v%s）", g_cfg.title, APP_VERSION_W);
        exp[1399] = 0;
        ok(wcscmp(t, exp) == 0, L"title without config version shows software version");
        ok(wcsstr(t, L"（软件 v" APP_VERSION_W L"）") != NULL,
           L"title without config version still contains 软件 v1.1.0");
        lstrcpynW(g_cfg.version, L"1.0.0", CFG_VAL_LEN);   /* 恢复配置版本供后续用例 */
    }

    /* ---- 2. 进度单一入口/边界 ---- */
    {
        const int in[] = { -7, 0, 55, 100, 150 };
        const int out[] = { 0, 0, 55, 100, 100 };
        for (int i = 0; i < 5; i++) {
            post_progress_pct(in[i]);
            LRESULT pos = SendMessageW(g_hprog, PBM_GETPOS, 0, 0);
            wchar_t t[32]; GetWindowTextW(g_hprogpct, t, 32);
            wchar_t exp[32]; _snwprintf(exp, 32, L"%d%%", out[i]);
            ok((int)pos == out[i] && wcscmp(t, exp) == 0, L"progress pct consistent");
        }
    }

    /* ---- 3. reset_task_ui ---- */
    {
        SetWindowTextW(g_hverdict, L"FAIL");
        SetWindowTextW(g_hfile[0], L"content0");
        SetWindowTextW(g_hfile[1], L"content1");
        set_static_text(g_hmonfile, L"hit", RGB(1, 2, 3));
        post_progress_pct(80);
        reset_task_ui(FALSE);
        wchar_t t[64] = { 0 };
        GetWindowTextW(g_hverdict, t, 64); ok(t[0] == 0, L"reset verdict");
        GetWindowTextW(g_hfile[0], t, 64); ok(t[0] == 0, L"reset file0");
        GetWindowTextW(g_hfile[1], t, 64); ok(t[0] == 0, L"reset file1");
        GetWindowTextW(g_hmonfile, t, 64); ok(t[0] == 0, L"reset monfile");
        ok((int)SendMessageW(g_hprog, PBM_GETPOS, 0, 0) == 0, L"reset progress");
        reset_task_ui(TRUE);
        GetWindowTextW(g_herr, t, 64);
        ok(wcsstr(t, L"正常") != NULL, L"reset new session -> ok badge");
    }

    /* ---- 4. 设备行 26 盘不越界 ---- */
    {
        g_dev_mask = 0;
        for (int d = 0; d < 26; d++) g_dev_mask |= (1u << d);
        update_device_text();
        wchar_t* b = (wchar_t*)malloc(65536 * sizeof(wchar_t));
        GetWindowTextW(g_hdev, b, 65536);
        int n1 = (int)wcslen(b);
        GetWindowTextW(g_hlabel, b, 65536);
        int n2 = (int)wcslen(b);
        wprintf(L"  device len=%d label len=%d\n", n1, n2);
        ok(n1 > 0 && n1 < 8192, L"device text bounded");
        ok(n2 >= 0 && n2 < 8192, L"label text bounded");
        free(b);
        g_dev_mask = 0;
        update_device_text();
    }

    /* ---- 5. 最小尺寸按 DPI ---- */
    {
        MINMAXINFO mm; memset(&mm, 0, sizeof(mm));
        g_dpi = 96;
        wnd_proc(g_hmain, WM_GETMINMAXINFO, 0, (LPARAM)&mm);
        ok(mm.ptMinTrackSize.x == dpi_scale(880) && mm.ptMinTrackSize.y == dpi_scale(600),
           L"minmax 96dpi");
        int w96 = mm.ptMinTrackSize.x;
        memset(&mm, 0, sizeof(mm));
        g_dpi = 192;
        wnd_proc(g_hmain, WM_GETMINMAXINFO, 0, (LPARAM)&mm);
        ok(mm.ptMinTrackSize.x == w96 * 2, L"minmax 192dpi doubles");
        g_dpi = 96;
    }

    /* ---- 6. 单操作显示名 ---- */
    ok(wcscmp(op_display_name(OP_COPY), L"复制") == 0, L"op name copy");
    ok(wcscmp(op_display_name(OP_FORMAT), L"格式化") == 0, L"op name format");
    ok(wcscmp(op_display_name(OP_RWTEST), L"读写测试") == 0, L"op name rwtest");

    /* ---- 7. 配置窗口初始选中当前机型 ---- */
    {
        memset(&g_cfg, 0, sizeof(g_cfg));
        g_cfg.nmodels = 3;
        lstrcpynW(g_cfg.models[0].internal, L"M0", CFG_KEY_LEN);
        lstrcpynW(g_cfg.models[1].internal, L"M1", CFG_KEY_LEN);
        lstrcpynW(g_cfg.models[2].internal, L"M2", CFG_KEY_LEN);
        g_cur_model = 2;
        ok(cfg_current_model_sel() == 2, L"cfg sel current");
        g_cur_model = 99;
        ok(cfg_current_model_sel() == 0, L"cfg sel fallback");
    }

    /* ---- 8. STATIC 设色（WM_CTLCOLORSTATIC） ---- */
    {
        set_static_text(g_hmonfile, L"color-test", RGB(10, 20, 30));
        HDC hdc = GetDC(g_hmain);
        LRESULT br = wnd_proc(g_hmain, WM_CTLCOLORSTATIC, (WPARAM)hdc, (LPARAM)g_hmonfile);
        ok(GetTextColor(hdc) == RGB(10, 20, 30), L"static text color");
        ok(br != 0, L"static brush returned");
        ReleaseDC(g_hmain, hdc);
    }

    /* ---- 9. 热键（Ctrl+Alt+T/C）注册/注销 ---- */
    {
        HWND hw = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 10, 10,
                                  NULL, NULL, GetModuleHandleW(NULL), NULL);
        ok(hw != NULL, L"hotkey test window");
        BOOL r = hotkeys_enable(hw, TRUE);
        wprintf(L"  hotkeys_enable(TRUE)=%d\n", r);
        ok(r == TRUE, L"hotkeys register Ctrl+Alt+T/C");
        hotkeys_enable(hw, FALSE);
        ok(hotkeys_enable(hw, FALSE) == TRUE, L"hotkeys disable idempotent");
        DestroyWindow(hw);
    }

    DestroyWindow(g_hmain);
    wprintf(L"\n[test_gui_state] PASS=%d FAIL=%d\n", pass, fail);
    return fail ? 1 : 0;
}
