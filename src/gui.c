/* ============================================================
 * gui.c - 主窗口（WND_MAIN）+ 配置窗口（WND_CONFIG）
 * DPI：Per-Monitor V2（运行时声明），所有尺寸经 DPI 缩放
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "resource.h"
#include "util.h"
#include "device.h"
#include "ops.h"
#include "version.h"
#include "default_ini.h"

/* ---------- 全局 ---------- */
CONFIG g_cfg;
wchar_t g_ini_path[MAX_PATH] = L"";
volatile LONG g_running = 0;

static HINSTANCE g_hinst = NULL;
static HWND g_hmain = NULL;
static HWND g_hmodel = NULL, g_hlabel = NULL, g_hdev = NULL,
            g_herr = NULL, g_hprog = NULL, g_hcmd[2] = { NULL, NULL },
            g_hverdict = NULL, g_hfile[2] = { NULL, NULL }, g_hchk_top = NULL,
            g_hchk_hotkey = NULL, g_hautomode = NULL, g_hsrcpath = NULL,
            g_hcount = NULL, g_hprogtitle = NULL, g_hprogpct = NULL;
static volatile LONG g_inscount = 0;    /* 插拔计数器：每次插入 +1（不持久化，退出即清零） */
static BOOL g_hotkey_enabled = FALSE;   /* 快捷键使能（IDC_CHK_HOTKEY） */
static HFONT g_font = NULL, g_font_mono = NULL, g_font_bold = NULL;
static UINT g_dpi = 96;
static DWORD g_dev_mask = 0;
static int g_cur_model = 0;         /* 当前机型索引（与轮询解耦） */
static int g_ident_index = 0;       /* 标识生成计数 */

/* ---------- 监控窗格（PANE_BOTTOM） ---------- */
static volatile LONG g_monitoring = 0;              /* 监控开关 */
static HWND g_hmonbtn[3] = { NULL, NULL, NULL };    /* 开始/停止/退出 */
static HWND g_hmonstatus = NULL, g_hmonfile = NULL;
static const COLORREF g_btncol[3] = {
    RGB(0, 150, 0),     /* 开始监控：绿 */
    RGB(210, 0, 0),     /* 停止监控：红 */
    RGB(170, 170, 170), /* 退出：灰 */
};

/* ---------- 前向声明 ---------- */
static void update_srcpath_text(void);
static void set_run_state(BOOL is_auto);
static void refresh_run_state(void);

/* ---------- DPI 辅助 ---------- */
static UINT get_system_dpi(void)
{
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef UINT(WINAPI* FN)(void);
        FN f = (FN)GetProcAddress(u32, "GetDpiForSystem");
        if (f) return f();
    }
    HDC dc = GetDC(NULL);
    UINT dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return dpi;
}

static int dpi_scale(int v) { return MulDiv(v, (int)g_dpi, 96); }

static void dpi_init(void)
{
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef BOOL(WINAPI* FN_SetCtx)(HANDLE);
        FN_SetCtx setctx = (FN_SetCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (setctx) {
            /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = ((HANDLE)-4) */
            if (setctx((HANDLE)-4)) return;
        }
    }
    HMODULE shc = LoadLibraryW(L"shcore.dll");
    if (shc) {
        typedef HRESULT(WINAPI* FN_SetProc)(int);
        FN_SetProc sp = (FN_SetProc)GetProcAddress(shc, "SetProcessDpiAwareness");
        if (sp) sp(2 /* PROCESS_PER_MONITOR_DPI_AWARE */);
        else {
            typedef BOOL(WINAPI* FN_SetDef)(void);
            FN_SetDef sd = (FN_SetDef)GetProcAddress(shc, "SetProcessDPIAware");
            if (sd) sd();
        }
        FreeLibrary(shc);
    }
}

/* EnumChildWindows 回调：广播字体 */
static BOOL CALLBACK font_enum(HWND hc, LPARAM lp)
{
    SendMessageW(hc, WM_SETFONT, (WPARAM)lp, TRUE);
    return TRUE;
}

static void dpi_update_font(void)
{
    if (g_font) DeleteObject(g_font);
    if (g_font_mono) DeleteObject(g_font_mono);
    if (g_font_bold) DeleteObject(g_font_bold);
    int h1 = -MulDiv(9, (int)g_dpi, 72);
    g_font = CreateFontW(h1, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_mono = CreateFontW(h1, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    g_font_bold = CreateFontW(h1, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                              0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    if (g_hmain) {
        EnumChildWindows(g_hmain, font_enum, (LPARAM)g_font);
        if (g_hcmd[0]) SendMessageW(g_hcmd[0], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hcmd[1]) SendMessageW(g_hcmd[1], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hfile[0]) SendMessageW(g_hfile[0], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hfile[1]) SendMessageW(g_hfile[1], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hdev)    SendMessageW(g_hdev, WM_SETFONT, (WPARAM)g_font_bold, TRUE);  /* 设备状态粗体 */
        if (g_hcount)  SendMessageW(g_hcount, WM_SETFONT, (WPARAM)g_font_bold, TRUE); /* 插拔计数粗体 */
    }
}

/* ---------- 控制台（RichEdit 追加行） ---------- */

static COLORREF color_of(int lv)
{
    switch (lv) {
    case LOG_OK:   return RGB(0, 150, 0);
    case LOG_WARN: return RGB(180, 120, 0);
    case LOG_ERR:  return RGB(200, 0, 0);
    case LOG_UI:   return RGB(0, 90, 200);
    default:       return RGB(60, 60, 60);
    }
}

static void con_append(HWND h, const wchar_t* line, LOGLEVEL lv)
{
    if (!h || !line) return;
    /* 限制长度（超限清空，防内存增长） */
    GETTEXTLENGTHEX gtl;
    gtl.flags = GTL_NUMCHARS;
    gtl.codepage = 1200;
    LRESULT len = SendMessageW(h, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);
    if (len > 120000) SetWindowTextW(h, L"");

    CHARRANGE cr;
    cr.cpMin = cr.cpMax = -1;                 /* 末尾 */
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&cr);
    wchar_t full[2200];
    _snwprintf(full, 2200, L"%s\r\n", line);
    SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)full);

    /* 给本行上色 */
    CHARRANGE sel;
    SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&sel);
    long endpos = (long)sel.cpMax;
    long startpos = endpos - (long)wcslen(line) - 2;
    if (startpos < 0) startpos = 0;
    CHARRANGE sel2;
    sel2.cpMin = startpos;
    sel2.cpMax = endpos;
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&sel2);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = color_of(lv);
    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    /* 光标回末尾并滚到底 */
    CHARRANGE cr3;
    cr3.cpMin = cr3.cpMax = -1;
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&cr3);
    SendMessageW(h, WM_VSCROLL, SB_BOTTOM, 0);
}

/* ---------- 控件状态刷新 ---------- */

static void refresh_buttons(void)
{
    if (!g_hmain || g_cfg.nmodels <= 0) return;
    CFGMODEL* m = &g_cfg.models[g_cur_model];
    BOOL busy = g_running != 0;

    struct { int id; const wchar_t* key; const wchar_t* def; const wchar_t* enablekey; } btns[] = {
        { IDC_BTN_START,      L"BTN_START",      L"开始",     NULL },
        { IDC_BTN_STOP,       L"BTN_STOP",       L"停止",     NULL },
        { IDC_BTN_WRITELABEL, L"BTN_WRITELABEL", L"复制",     L"ENABLE_WRITE" },
        { IDC_BTN_READCHECK,  L"BTN_READCHECK",  L"读取校验", L"ENABLE_READ" },
        { IDC_BTN_RWTEST,     L"BTN_RWTEST",     L"读写测试", NULL },
        { IDC_BTN_FORMAT,     L"BTN_FORMAT",     L"格式化",   L"ENABLE_FORMAT" },
        { IDC_BTN_READLABEL,  L"BTN_READLABEL",  L"读取标签", NULL },
        { IDC_BTN_CONFIG,     NULL,              L"配置",     NULL },
    };
    for (int i = 0; i < 8; i++) {
        HWND hb = GetDlgItem(g_hmain, btns[i].id);
        BOOL enable = !busy;
        if (btns[i].enablekey) enable = enable && cfg_enabled(m, btns[i].enablekey);
        EnableWindow(hb, enable);
        if (btns[i].key) {
            wchar_t name[64];
            cfg_get_def(m, btns[i].key, btns[i].def, name, 64);
            SetWindowTextW(hb, name);
        }
    }
    EnableWindow(GetDlgItem(g_hmain, IDC_BTN_STOP), busy);

    update_srcpath_text();   /* 状态行同步显示当前机型 SRCPATH */
    refresh_run_state();     /* AUTO/MANUAL 随监控开关 / 机型 AUTORUN 统一刷新 */

    /* 监控按钮状态与颜色（自绘按钮需重绘） */
    BOOL mon = g_monitoring != 0;
    EnableWindow(g_hmonbtn[0], !mon);   /* 开始监控：关闭时可用（绿） */
    EnableWindow(g_hmonbtn[1], mon);    /* 停止监控：开启时可用（红） */
    EnableWindow(g_hmonbtn[2], TRUE);   /* 退出：常可用（灰） */
    for (int i = 0; i < 3; i++)
        if (g_hmonbtn[i]) InvalidateRect(g_hmonbtn[i], NULL, TRUE);
}

/* 徽标通用绘制：绿=正常提示，红=出错（红时标题加 [!]，绿/清空时去掉） */
static void badge_paint(const wchar_t* txt, COLORREF color, BOOL is_error)
{
    HWND h = g_herr;
    SetWindowTextW(h, txt);
    CHARFORMAT2W cf; memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR | CFM_BOLD;
    cf.dwEffects = CFE_BOLD; cf.crTextColor = color;
    SendMessageW(h, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    wchar_t t[256];
    GetWindowTextW(g_hmain, t, 200);
    wchar_t* bang = wcsstr(t, L" [!]");
    if (bang) { *bang = 0; SetWindowTextW(g_hmain, t); }
    if (is_error) {
        GetWindowTextW(g_hmain, t, 200);
        if (!wcsstr(t, L"[!]")) {
            wchar_t t2[260];
            _snwprintf(t2, 260, L"%s [!]", t);
            SetWindowTextW(g_hmain, t2);
        }
    }
}

/* 绿色正常提示（插盘/任务完成等） */
static void set_ok_badge(const wchar_t* msg)
{
    wchar_t txt[300];
    _snwprintf(txt, 300, L"✓ %s", msg && msg[0] ? msg : L"正常");
    badge_paint(txt, RGB(0, 150, 0), FALSE);
}

/* 红色出错提示 */
static void set_error_badge(BOOL on, const wchar_t* msg)
{
    if (on) {
        wchar_t txt[300];
        _snwprintf(txt, 300, L"✕ 出错%s%s", msg && msg[0] ? L"：" : L"", msg ? msg : L"");
        badge_paint(txt, RGB(200, 0, 0), TRUE);
    } else {
        SetWindowTextW(g_herr, L"");
        wchar_t t[256];
        GetWindowTextW(g_hmain, t, 200);
        wchar_t* bang = wcsstr(t, L" [!]");
        if (bang) { *bang = 0; SetWindowTextW(g_hmain, t); }
    }
}

static void set_text_color(HWND h, const wchar_t* txt, COLORREF color)
{
    SetWindowTextW(h, txt);
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD;
    cf.dwEffects = CFE_BOLD;
    cf.crTextColor = color;
    SendMessageW(h, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

static void set_verdict(const wchar_t* v)
{
    if (wcscmp(v, L"PASS") == 0)
        set_text_color(g_hverdict, L"PASS", RGB(0, 150, 0));
    else
        set_text_color(g_hverdict, L"FAIL", RGB(200, 0, 0));
}

/* 指定颜色显示判定徽标（复制结果等） */
static void set_verdict_colored(const wchar_t* txt, COLORREF color)
{
    set_text_color(g_hverdict, txt, color);
}

/* AUTO / MANUAL 状态标记：AUTO=绿，MANUAL=蓝
 * 注意：只允许由 refresh_run_state() 调用，避免各处散落的 set_run_state 互相覆盖 */
static void set_run_state(BOOL is_auto)
{
    if (!g_hautomode) return;
    if (is_auto)
        set_text_color(g_hautomode, L"AUTO", RGB(0, 140, 0));
    else
        set_text_color(g_hautomode, L"MANUAL", RGB(0, 90, 200));
}

/* IDC_STC_AUTOMODE 1104 的唯一判定点：
 *   AUTO   = 监控已开启(g_monitoring) 或 当前机型 AUTORUN=1 → 插盘会自动执行整任务
 *   MANUAL = 两者皆无 → 只能手动点按钮
 * AUTO_COPY / AUTO_TEST 只决定"自动流程内做哪几步"，不参与 AUTO/MANUAL 判定，
 * 否则会出现"配了自动复制却显示 MANUAL""自动跑完被手动按钮改成 MANUAL"之类的错乱。 */
static LONG g_run_state = -1;   /* -1 未知 / 0 MANUAL / 1 AUTO，仅用于变化时记日志 */

static BOOL run_auto_mode(void)
{
    if (g_monitoring) return TRUE;
    if (g_cfg.nmodels > 0 && g_cur_model >= 0 &&
        cfg_get_int(&g_cfg.models[g_cur_model], L"AUTORUN", 0)) return TRUE;
    return FALSE;
}

static void refresh_run_state(void)
{
    if (!g_hautomode) return;
    BOOL a = run_auto_mode();
    set_run_state(a);
    LONG v = a ? 1 : 0;
    if (v != g_run_state) {
        const wchar_t* why = a
            ? (g_monitoring ? L"监控已开启" : L"本机型 AUTORUN=1")
            : L"未开监控且本机型 AUTORUN=0";
        g_run_state = v;
        applog(CH_ENGINE, LOG_UI, L"[UI] 运行模式 %s（%s）", a ? L"AUTO" : L"MANUAL", why);
    }
}

/* 状态行显示当前机型 SRCPATH */
static void update_srcpath_text(void)
{
    if (!g_hsrcpath || g_cfg.nmodels <= 0) return;
    const wchar_t* p = cfg_get(&g_cfg.models[g_cur_model], L"SRCPATH");
    wchar_t txt[CFG_VAL_LEN + 32];
    if (p && p[0])
        _snwprintf(txt, CFG_VAL_LEN + 32, L"SRCPATH：%s", p);
    else
        lstrcpynW(txt, L"SRCPATH：（本机型未配置）", CFG_VAL_LEN + 32);
    SetWindowTextW(g_hsrcpath, txt);
}

/* 监控状态行：标题按颜色显示（停止=红 / 开启=绿 / 命中=黑）+ 副行蓝 */
static void set_monstatus(const wchar_t* title, const wchar_t* sub, COLORREF titlecol)
{
    set_text_color(g_hmonstatus, title ? title : L"", titlecol);
    if (sub && sub[0])
        set_text_color(g_hmonfile, sub, RGB(0, 90, 200));
    else
        SetWindowTextW(g_hmonfile, L"");
}

/* 插拔计数器：仅在监控开启时计数；显示"计数:0000"4 位循环（9999 后回 0000） */
static void update_count_text(void)
{
    if (!g_hcount) return;
    wchar_t ct[32];
    _snwprintf(ct, 32, L"计数:%04lu", (unsigned long)(g_inscount % 10000));
    SetWindowTextW(g_hcount, ct);
}

static void update_device_text(void)
{
    if (!g_dev_mask) {
        SetWindowTextW(g_hdev, L"设备状态：未检测到可移动磁盘（等待插入…）");
        SetWindowTextW(g_hlabel, L"标签：（无磁盘）");
        return;
    }
    wchar_t txt[512] = L"设备状态：";
    wchar_t lbl[512] = L"标签：";
    BOOL any = FALSE, lab = FALSE;
    for (wchar_t d = L'C'; d <= L'Z'; d++) {
        if (g_dev_mask & (1u << (d - L'A'))) {
            DEVINFO di;
            dev_get_info(d, &di);
            wchar_t one[160];
            _snwprintf(one, 160, L"%c: [%s|%.1fGB]  ", d,
                       di.volume[0] ? di.volume : L"无卷标",
                       di.total_bytes / 1073741824.0);
            lstrcatW(txt, one);
            _snwprintf(one, 160, L"%s%c: ", any ? L"  " : L"", d);
            if (di.volume[0]) {
                lstrcatW(lbl, one);
                lstrcatW(lbl, di.volume);
                lab = TRUE;
            }
            any = TRUE;
        }
    }
    if (!lab) lstrcatW(lbl, L"（当前磁盘均无卷标）");
    SetWindowTextW(g_hdev, txt);
    SetWindowTextW(g_hlabel, lbl);   /* IDC_STC_LABEL 1101 随插拔实时刷新 */
}

/* ---------- 任务执行 ---------- */

typedef struct {
    int model_index;
    int trigger;
    wchar_t drive;
} TASKARG;

static DWORD WINAPI task_thread(LPVOID p)
{
    TASKARG* a = (TASKARG*)p;
    CFGMODEL* m = &g_cfg.models[a->model_index];
    wchar_t drv = a->drive;

    applog(CH_ENGINE, LOG_UI, L"=== 任务开始：%s（%s，盘 %c:） v%s ===",
           m->display, a->trigger == TRIGGER_AUTO ? L"自动" : L"手动",
           drv, g_cfg.version);

    int rc = 0;
    BOOL manual = (a->trigger == TRIGGER_MANUAL);
    rc |= op_usbinfo(m, drv);

    /* 复制：手动触发总是执行；自动触发看 AUTO_COPY（1=自动，0=仅手动点[复制]） */
    if (!CHECK_STOP()) {
        if (manual || cfg_get_int(m, L"AUTO_COPY", 1)) {
            rc |= op_write(m, drv);
        } else {
            applog(CH_ENGINE, LOG_INFO,
                   L"[WRITE] 自动模式且 AUTO_COPY=0，跳过自动复制（可手动点击[复制]）");
        }
    }

    /* 读取 + 判定：ENABLE_READ 控制，自动/手动均执行（核心校验） */
    if (!CHECK_STOP()) rc |= op_read(m, drv);

    /* 读写测试：手动触发总是执行；自动触发看 AUTO_TEST（1=自动，0=仅手动点[读写测试]） */
    if (!CHECK_STOP()) {
        if (manual || cfg_get_int(m, L"AUTO_TEST", 0)) {
            rc |= op_rwtest(m, drv);
        } else {
            applog(CH_ENGINE, LOG_INFO,
                   L"[RWTEST] 自动模式且 AUTO_TEST=0，跳过自动测试（可手动点击[读写测试]）");
        }
    }

    applog(CH_ENGINE, rc == 0 ? LOG_OK : (rc == 2 ? LOG_WARN : LOG_ERR),
           L"=== 任务结束（%s） ===", rc == 0 ? L"成功" : rc == 2 ? L"已停止" : L"有错误");
    PostMessageW(g_hmain, WM_APP_TASKDONE, rc, 0);
    free(a);
    return 0;
}

void task_run_async(int model_index, int trigger)
{
    if (g_running) {
        applog(CH_ENGINE, LOG_WARN, L"任务执行中，忽略新请求");
        return;
    }
    DWORD mask = 0;
    dev_enum_removable(&mask, NULL);
    wchar_t drive = 0;
    for (wchar_t d = L'C'; d <= L'Z'; d++)
        if (mask & (1u << (d - L'A'))) { drive = d; break; }
    if (!drive) {
        applog(CH_ENGINE, LOG_ERR, L"没有可移动磁盘");
        PostMessageW(g_hmain, WM_APP_ERROR, 1, (LPARAM)wcs_dup(L"没有可移动磁盘"));
        return;
    }
    InterlockedExchange(&g_running, 1);
    TASKARG* a = (TASKARG*)malloc(sizeof(TASKARG));
    if (!a) return;
    a->model_index = model_index;
    a->trigger = trigger;
    a->drive = drive;
    refresh_run_state();   /* 运行模式由监控开关/AUTORUN 决定，不由本次触发方式决定 */
    HANDLE h = CreateThread(NULL, 0, task_thread, a, 0, NULL);
    if (h) CloseHandle(h);
    else { InterlockedExchange(&g_running, 0); free(a); }
}

/* ---------- 单操作线程 ---------- */

enum { OP_FORMAT = 0, OP_LABELREAD = 1, OP_COPY = 2, OP_IDENT = 3,
       OP_READ = 4, OP_RWTEST = 5 };

typedef struct {
    int op;
    int model_index;
    wchar_t drive;
} OPARG;

static DWORD WINAPI op_thread(LPVOID p)
{
    OPARG* a = (OPARG*)p;
    CFGMODEL* m = &g_cfg.models[a->model_index];
    int rc = 0;
    switch (a->op) {
    case OP_FORMAT:     rc = op_format(m, a->drive, g_hmain); break;
    case OP_LABELREAD:  rc = op_label_read(m, a->drive); break;
    case OP_COPY:
        rc = op_write(m, a->drive);
        /* 复制结果徽标：成功绿 Copy Complete! / 失败红 Copy Failure! */
        PostMessageW(g_hmain, WM_APP_VERDICT,
                     rc == 0 ? 1 : 2,
                     (LPARAM)wcs_dup(rc == 0 ? L"Copy Complete!" : L"Copy Failure!"));
        break;
    case OP_IDENT:      g_ident_index++; rc = op_gen_ident(m, a->drive, g_ident_index); break;
    case OP_READ:       rc = op_read(m, a->drive); break;      /* 判定徽标由 op_read 内部设置 */
    case OP_RWTEST:     rc = op_rwtest(m, a->drive); break;
    }
    PostMessageW(g_hmain, WM_APP_TASKDONE, rc, 0);
    free(a);
    return 0;
}

static wchar_t get_first_drive(void)
{
    DWORD mask = 0;
    dev_enum_removable(&mask, NULL);
    for (wchar_t d = L'C'; d <= L'Z'; d++)
        if (mask & (1u << (d - L'A'))) return d;
    return 0;
}

static void run_single(int op)
{
    if (g_running) { applog(CH_ENGINE, LOG_WARN, L"任务执行中，请等待完成或停止"); return; }
    wchar_t drive = get_first_drive();
    if (!drive) {
        applog(CH_ENGINE, LOG_ERR, L"没有可移动磁盘");
        PostMessageW(g_hmain, WM_APP_ERROR, 1, (LPARAM)wcs_dup(L"没有可移动磁盘"));
        return;
    }
    InterlockedExchange(&g_running, 1);
    OPARG* a = (OPARG*)malloc(sizeof(OPARG));
    if (!a) { InterlockedExchange(&g_running, 0); return; }
    a->op = op;
    a->model_index = g_cur_model;
    a->drive = drive;
    refresh_run_state();   /* 单操作不改变运行模式：监控开着仍显示 AUTO */
    HANDLE h = CreateThread(NULL, 0, op_thread, a, 0, NULL);
    if (h) CloseHandle(h);
    else { InterlockedExchange(&g_running, 0); free(a); }
}

/* ---------- 布局 ---------- */

static void layout(void)
{
    if (!g_hmain) return;
    RECT rc;
    GetClientRect(g_hmain, &rc);
    int W = rc.right, H = rc.bottom;
    int m = dpi_scale(8);
    int rowh = dpi_scale(26);
    int y = m;

    /* PANE_TOP 第一行：机型下拉 + 操作按钮（8 个按钮统一尺寸，文字居中）
     * 注意：COMBO 的 MoveWindow 高度是"含下拉列表"的总高度，必须给足 */
    MoveWindow(g_hmodel, m, y, dpi_scale(200), dpi_scale(180), TRUE);
    int x = m + dpi_scale(206);
    int opw = dpi_scale(88);   /* 统一按钮宽度 */
    int opids[] = { IDC_BTN_START, IDC_BTN_STOP, IDC_BTN_WRITELABEL,
                    IDC_BTN_READCHECK, IDC_BTN_RWTEST, IDC_BTN_FORMAT, IDC_BTN_CONFIG };
    for (int i = 0; i < 7; i++) {
        HWND hb = GetDlgItem(g_hmain, opids[i]);
        MoveWindow(hb, x, y, opw, rowh, TRUE);   /* BS_PUSHBUTTON 文字默认水平/垂直居中 */
        x += opw + dpi_scale(4);
    }

    /* PANE_TOP 第二行：读取标签 + 置顶 + 快捷键使能（按钮与复选框拉开间距） */
    int y2 = y + rowh + dpi_scale(4);
    MoveWindow(GetDlgItem(g_hmain, IDC_BTN_READLABEL), m, y2, opw, rowh, TRUE);
    int chkx = m + opw + dpi_scale(12);
    MoveWindow(g_hchk_top, chkx, y2, dpi_scale(80), rowh, TRUE);
    MoveWindow(g_hchk_hotkey, chkx + dpi_scale(80) + dpi_scale(8), y2, dpi_scale(96), rowh, TRUE);

    y = y2 + rowh + dpi_scale(6);
    /* PANE_LABEL 标签行 */
    MoveWindow(g_hlabel, m, y, W - 2 * m, rowh, TRUE);
    y += rowh + dpi_scale(2);

    /* PANE_STATUS 状态行：AUTO/MANUAL 标记 + SRCPATH + 插拔计数器 */
    int modew = dpi_scale(76);
    int cntw = dpi_scale(110);
    MoveWindow(g_hautomode, m, y, modew, rowh, TRUE);
    MoveWindow(g_hsrcpath, m + modew + dpi_scale(8), y + dpi_scale(4),
               W - 2 * m - modew - cntw - dpi_scale(16), rowh, TRUE);
    MoveWindow(g_hcount, W - m - cntw, y + dpi_scale(4), cntw, rowh, TRUE);
    y += rowh + dpi_scale(12);   /* 设备状态行及以下整体下移，与状态行拉开间距 */

    /* 设备状态 + 出错徽标 */
    int devw = (W - 2 * m) * 2 / 3;
    MoveWindow(g_hdev, m, y, devw, rowh, TRUE);
    MoveWindow(g_herr, m + devw + dpi_scale(6), y,
               W - 2 * m - devw - dpi_scale(6), rowh, TRUE);
    y += rowh + dpi_scale(6);
    /* PANE_TASK 进度条：前提示文字 + 进度条 + 尾百分比 */
    int lblw = dpi_scale(66), pctw = dpi_scale(44);
    MoveWindow(g_hprogtitle, m, y + dpi_scale(3), lblw, rowh, TRUE);
    MoveWindow(g_hprog, m + lblw + dpi_scale(6), y + dpi_scale(5),
               W - 2 * m - lblw - pctw - dpi_scale(12), dpi_scale(16), TRUE);
    MoveWindow(g_hprogpct, W - m - pctw, y + dpi_scale(3), pctw, rowh, TRUE);
    y += rowh + dpi_scale(10);

    /* PANE_CMDS：CMD-A / CMD-B 左右并排 */
    int remaining = H - y - dpi_scale(26) - m;
    int cmdh = remaining / 2;
    if (cmdh < dpi_scale(60)) cmdh = dpi_scale(60);
    int half = (W - 2 * m - dpi_scale(6)) / 2;
    MoveWindow(g_hcmd[0], m, y, half, cmdh, TRUE);
    MoveWindow(g_hcmd[1], m + half + dpi_scale(6), y,
               W - 2 * m - half - dpi_scale(6), cmdh, TRUE);
    y += cmdh + dpi_scale(4);

    /* 底部监控窗格（PANE_BOTTOM）：判定徽标 + 状态行/清屏 → 命中文件行 → 内容区 → 按钮行 */
    MoveWindow(g_hverdict, m, y, dpi_scale(70), rowh, TRUE);
    int clr_w = dpi_scale(56);
    MoveWindow(g_hmonstatus, m + dpi_scale(78), y,
               W - 2 * m - dpi_scale(78) - clr_w - dpi_scale(6), rowh, TRUE);
    MoveWindow(GetDlgItem(g_hmain, IDC_BTN_CLEARCMD),
               W - m - clr_w, y, clr_w, rowh, TRUE);
    y += rowh + dpi_scale(2);
    MoveWindow(g_hmonfile, m, y, W - 2 * m, rowh, TRUE);
    y += rowh + dpi_scale(2);

    int btnrow = rowh + dpi_scale(8);
    int fileh = H - y - btnrow - m * 2;
    if (fileh < dpi_scale(40)) fileh = dpi_scale(40);
    /* PANE_FILE：文件内容左右双窗格（与 PANE_CMDS 同布局），左=第一命中，右=第二命中 */
    int fhalf = (W - 2 * m - dpi_scale(6)) / 2;
    MoveWindow(g_hfile[0], m, y, fhalf, fileh, TRUE);
    MoveWindow(g_hfile[1], m + fhalf + dpi_scale(6), y,
               W - 2 * m - fhalf - dpi_scale(6), fileh, TRUE);
    y += fileh + dpi_scale(6);

    /* 三个监控按钮：水平居中 */
    int bw = dpi_scale(92), gap = dpi_scale(18);
    int bx = (W - (bw * 3 + gap * 2)) / 2;
    if (bx < m) bx = m;
    MoveWindow(g_hmonbtn[0], bx, y, bw, rowh, TRUE);
    MoveWindow(g_hmonbtn[1], bx + bw + gap, y, bw, rowh, TRUE);
    MoveWindow(g_hmonbtn[2], bx + (bw + gap) * 2, y, bw, rowh, TRUE);
}

/* ============================================================
 * 配置窗口
 * ============================================================ */

static BOOL g_cfg_unlocked = FALSE;
static HWND g_hcfg = NULL, g_hcfglist = NULL, g_hcfgtext = NULL, g_hcfgpass = NULL,
            g_hcfgstate = NULL, g_hcfgkey = NULL, g_hcfgval = NULL,
            g_hcfgvalcombo = NULL, g_hcfghint = NULL;
static int g_cfg_failcount = 0;

/* 键的简要说明（配置窗口提示行） */
static const wchar_t* key_hint(const wchar_t* key)
{
    static const struct { const wchar_t* k; const wchar_t* h; } tbl[] = {
        { L"ENABLE_READ",     L"读取开关：1=允许读取（读取校验按钮可用、自动流程执行判定）；0=禁用并置灰" },
        { L"ENABLE_WRITE",    L"写入开关：1=允许复制文件（复制按钮可用）；0=禁用并置灰" },
        { L"ENABLE_FORMAT",   L"格式化开关：1=格式化按钮可用；0=禁用并置灰（自动流程永不格式化）" },
        { L"ENABLE_USBINFO",  L"USB 识别开关：1=识别 USB2.0/3.x 通讯模式并输出到 CMD-A；0=跳过" },
        { L"AUTO_COPY",       L"自动复制：1=插盘自动流程内复制（需 AUTORUN=1 或开启监控）；0=只手动点[复制]" },
        { L"AUTO_TEST",       L"自动测试：1=插盘自动流程内执行读写测试；0=只手动点[读写测试]" },
        { L"AUTORUN",         L"插盘自动执行：1=插入U盘立即执行整任务；0=需先点[开始监控]" },
        { L"WRITEMODE",       L"写入方式：COPY=简单复制（推荐）；PROCESSED=按台生成不同文件（后续版本）" },
        { L"SRCPATH",         L"复制源：绝对路径或相对路径（相对=exe 同目录），支持 * 与 ? 通配符" },
        { L"DSTPATH",         L"U盘内目标目录：目录不存在时自动逐级建立；\\ 表示U盘根目录" },
        { L"READFILE",        L"读取文件名：U盘根目录，支持通配符，如 8685????.txt；留空=跳过读取" },
        { L"MATCH_POLICY",    L"多命中取舍：NEWEST=取最新 / FIRST=第一个 / STRICT=多命中判失败并列出" },
        { L"READPATTERN",     L"匹配方式：WILDCARD=通配符（推荐）；REGEX=正则（后续版本支持）" },
        { L"READRULE",        L"判定规则（分号分隔）：MUST_CONTAIN:关键字 ; MIN_LEN:最小长度；PASS 才显示内容" },
        { L"CONFIRM",         L"格式化确认：1=格式化前弹窗二次确认（建议保持 1）" },
        { L"FILESYSTEM",      L"格式化文件系统：FAT32（≤32GB/单文件≤4GB）或 EXFAT（大容量推荐）" },
        { L"VOLUME",          L"卷标：仅格式化完成后写入；留空=不改卷标（FAT32 卷标最长 11 字符）" },
        { L"HASH",            L"复制校验算法：md5（复制前后比对，不一致判失败）" },
        { L"ADMINPASS",       L"配置解锁密码（明文存放，仅用于防误改；留空=禁止解锁编辑）" },
        { L"PRJNUM",          L"机型编号：程序内部映射用，改动会导致配置识别不到该机型" },
        { L"TITLE",           L"主窗口标题文字（仅影响显示，不影响功能）" },
        { L"VERSION",         L"配置版本标记（仅作记录，不参与程序逻辑）" },
    };
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++)
        if (_wcsicmp(key, tbl[i].k) == 0) return tbl[i].h;
    if (_wcsnicmp(key, L"BTN_", 4) == 0)
        return L"按钮显示名：仅修改界面文字（如“写入标签”→复制、升级），不影响功能与日志";
    return L"（该键暂无说明，请参考 docs/配置文件使用手册.md）";
}

/* ---------- 配置项控件类型判定：枚举键用下拉框，其余用输入框 ---------- */

static void cfg_apply_lock(void);   /* 前向声明 */

/* 返回该键的可选值列表数量（0=自由输入） */
static int key_enum_values(const wchar_t* key, const wchar_t** vals, int max)
{
    static const wchar_t* yn[]   = { L"0", L"1" };
    static const wchar_t* wmode[] = { L"COPY", L"PROCESSED" };
    static const wchar_t* policy[] = { L"NEWEST", L"FIRST", L"STRICT" };
    static const wchar_t* pattern[] = { L"WILDCARD", L"REGEX" };
    static const wchar_t* fs[]   = { L"FAT32", L"EXFAT" };
    static const wchar_t* hashv[] = { L"md5" };

    const wchar_t** src = NULL;
    int n = 0;
    if (_wcsnicmp(key, L"ENABLE_", 7) == 0 ||
        _wcsicmp(key, L"CONFIRM") == 0 ||
        _wcsicmp(key, L"AUTORUN") == 0 ||
        _wcsicmp(key, L"AUTO_COPY") == 0 ||
        _wcsicmp(key, L"AUTO_TEST") == 0) {
        src = yn; n = 2;
    } else if (_wcsicmp(key, L"WRITEMODE") == 0) {
        src = wmode; n = 2;
    } else if (_wcsicmp(key, L"MATCH_POLICY") == 0) {
        src = policy; n = 3;
    } else if (_wcsicmp(key, L"READPATTERN") == 0) {
        src = pattern; n = 2;
    } else if (_wcsicmp(key, L"FILESYSTEM") == 0) {
        src = fs; n = 2;
    } else if (_wcsicmp(key, L"HASH") == 0) {
        src = hashv; n = 1;
    }
    if (!src) return 0;
    if (n > max) n = max;
    for (int i = 0; i < n; i++) vals[i] = src[i];
    return n;
}

/* 按当前键切换"新值"控件：枚举→下拉；普通（SRCPATH/DSTPATH/READFILE 等）→输入框
 * fill_value=TRUE 时把配置里的当前值回填到值控件（列表选择时）；
 * 手动键入键名时 fill_value=FALSE，避免打断正在进行的输入 */
static void cfg_sync_value_editor_ex(BOOL fill_value)
{
    wchar_t key[CFG_KEY_LEN];
    GetWindowTextW(g_hcfgkey, key, CFG_KEY_LEN);   /* 兼容手输键名（CBS_DROPDOWN） */
    if (!key[0]) return;
    int mi = (int)SendMessageW(g_hcfglist, LB_GETCURSEL, 0, 0);
    if (mi < 0 || mi >= g_cfg.nmodels) return;

    SetWindowTextW(g_hcfghint, key_hint(key));   /* 键值说明提示 */
    CFGMODEL* m = &g_cfg.models[mi];
    const wchar_t* cur = cfg_get(m, key);
    if (!cur) cur = L"";

    const wchar_t* vals[4];
    int n = key_enum_values(key, vals, 4);
    if (n > 0) {
        ShowWindow(g_hcfgvalcombo, SW_SHOW);
        ShowWindow(g_hcfgval, SW_HIDE);
        SendMessageW(g_hcfgvalcombo, CB_RESETCONTENT, 0, 0);
        int pick = 0;
        for (int i = 0; i < n; i++) {
            SendMessageW(g_hcfgvalcombo, CB_ADDSTRING, 0, (LPARAM)vals[i]);
            if (_wcsicmp(vals[i], cur) == 0) pick = i;
        }
        SendMessageW(g_hcfgvalcombo, CB_SETCURSEL, pick, 0);
    } else {
        ShowWindow(g_hcfgvalcombo, SW_HIDE);
        ShowWindow(g_hcfgval, SW_SHOW);
        if (fill_value) SetWindowTextW(g_hcfgval, cur);
    }
    cfg_apply_lock();
}

static void cfg_sync_value_editor(void)
{
    cfg_sync_value_editor_ex(TRUE);
}

static void cfg_show_model(void)
{
    int sel = (int)SendMessageW(g_hcfglist, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= g_cfg.nmodels) return;
    CFGMODEL* m = &g_cfg.models[sel];
    wchar_t* buf = (wchar_t*)malloc(65536 * sizeof(wchar_t));
    if (!buf) return;
    buf[0] = 0;
    wchar_t head[256];
    _snwprintf(head, 256, L"机型：%s（内部键 %s）\r\n\r\n---- 配置键值（只读展示）----\r\n",
               m->display, m->internal);
    lstrcatW(buf, head);
    for (int i = 0; i < m->nkeys; i++) {
        wchar_t line[CFG_KEY_LEN + CFG_VAL_LEN + 8];
        _snwprintf(line, CFG_KEY_LEN + CFG_VAL_LEN + 8, L"%s=%s\r\n",
                   m->keys[i].key, m->keys[i].val);
        lstrcatW(buf, line);
    }
    SetWindowTextW(g_hcfgtext, buf);
    SendMessageW(g_hcfgtext, EM_SETREADONLY, TRUE, 0);
    free(buf);

    /* 键名下拉：列出本机型全部键 */
    SendMessageW(g_hcfgkey, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < m->nkeys; i++)
        SendMessageW(g_hcfgkey, CB_ADDSTRING, 0, (LPARAM)m->keys[i].key);
    if (m->nkeys > 0) SendMessageW(g_hcfgkey, CB_SETCURSEL, 0, 0);
    cfg_sync_value_editor();
}

static void cfg_apply_lock(void)
{
    EnableWindow(g_hcfgkey, g_cfg_unlocked);
    EnableWindow(g_hcfgval, g_cfg_unlocked);
    EnableWindow(g_hcfgvalcombo, g_cfg_unlocked && IsWindowVisible(g_hcfgvalcombo));
    SetWindowTextW(g_hcfgstate, g_cfg_unlocked ? L"状态：已解锁（可编辑保存）" : L"状态：只读（未授权）");
}

/* 从磁盘重载配置并刷新主界面（机型下拉/按钮状态） */
static BOOL reload_config_and_ui(void)
{
    wchar_t cur_internal[CFG_KEY_LEN];
    lstrcpynW(cur_internal, g_cfg.models[g_cur_model].internal, CFG_KEY_LEN);
    CONFIG newcfg;
    if (!cfg_load(g_ini_path, &newcfg)) return FALSE;
    g_cfg = newcfg;
    int newsel = 0;
    SendMessageW(g_hmodel, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_cfg.nmodels; i++) {
        SendMessageW(g_hmodel, CB_ADDSTRING, 0, (LPARAM)g_cfg.models[i].display);
        if (_wcsicmp(g_cfg.models[i].internal, cur_internal) == 0) newsel = i;
    }
    g_cur_model = newsel;
    SendMessageW(g_hmodel, CB_SETCURSEL, newsel, 0);
    refresh_buttons();
    return TRUE;
}

/* 用内嵌出厂默认配置覆盖 ini */
static BOOL write_default_ini(void)
{
    HANDLE h = CreateFileW(g_ini_path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w = 0;
    static const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
    WriteFile(h, bom, 3, &w, NULL);
    WriteFile(h, DEFAULT_INI_UTF8, (DWORD)strlen(DEFAULT_INI_UTF8), &w, NULL);
    CloseHandle(h);
    return TRUE;
}

static void cfg_create_controls(HWND h)
{
    int m = dpi_scale(10), rowh = dpi_scale(24), y = m;
    HFONT f = g_font;

    #define CFGSTC(id, txt, x2, y2, w2, h2) do { \
        HWND hs = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE | SS_LEFT, \
                                  x2, y2, w2, h2, h, (HMENU)(INT_PTR)(id), g_hinst, NULL); \
        SendMessageW(hs, WM_SETFONT, (WPARAM)f, TRUE); } while (0)
    #define CFGLIST(id, x2, y2, w2, h2, style) do { \
        HWND hc = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL, \
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | (style), x2, y2, w2, h2, \
                    h, (HMENU)(INT_PTR)(id), g_hinst, NULL); \
        SendMessageW(hc, WM_SETFONT, (WPARAM)f, TRUE); } while (0)
    #define CFGEDIT(id, x2, y2, w2, h2, style) do { \
        HWND hc = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL, \
                    WS_CHILD | WS_VISIBLE | (style), x2, y2, w2, h2, \
                    h, (HMENU)(INT_PTR)(id), g_hinst, NULL); \
        SendMessageW(hc, WM_SETFONT, (WPARAM)f, TRUE); } while (0)
    #define CFGCOMBO(id, x2, y2, w2, h2, style) do { \
        HWND hc = CreateWindowExW(0, L"COMBOBOX", NULL, \
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | (style), \
                    x2, y2, w2, h2, h, (HMENU)(INT_PTR)(id), g_hinst, NULL); \
        SendMessageW(hc, WM_SETFONT, (WPARAM)f, TRUE); } while (0)
    #define CFGBTN(id, txt, x2, y2, w2, h2) do { \
        HWND hc = CreateWindowExW(0, L"BUTTON", txt, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, \
                                  x2, y2, w2, h2, h, (HMENU)(INT_PTR)(id), g_hinst, NULL); \
        SendMessageW(hc, WM_SETFONT, (WPARAM)f, TRUE); } while (0)

    CFGSTC(0, L"机型：", m, y + dpi_scale(4), dpi_scale(50), rowh);
    CFGLIST(IDC_CFG_LIST_MODELS, m + dpi_scale(50), y, dpi_scale(240), rowh * 4,
            LBS_NOTIFY | LBS_NOINTEGRALHEIGHT);
    y += rowh * 4 + dpi_scale(8);

    CFGSTC(0, L"键（选择）：", m, y + dpi_scale(4), dpi_scale(160), rowh);
    CFGCOMBO(IDC_CFG_COMBO_KEY, m + dpi_scale(165), y, dpi_scale(320),
             rowh + dpi_scale(150), CBS_DROPDOWN);   /* 可输入：允许键入列表外的新键名 */
    y += rowh + dpi_scale(4);
    CFGSTC(0, L"新值：", m, y + dpi_scale(4), dpi_scale(160), rowh);
    CFGEDIT(IDC_EDIT_CFGVAL, m + dpi_scale(165), y, dpi_scale(320), rowh, ES_AUTOHSCROLL);
    CFGCOMBO(IDC_CFG_COMBO_VAL, m + dpi_scale(165), y, dpi_scale(320),
             rowh + dpi_scale(150), CBS_DROPDOWNLIST);
    y += rowh + dpi_scale(4);

    /* 键值说明提示（选中键后自动显示） */
    CFGSTC(0, L"说明：", m, y + dpi_scale(4), dpi_scale(50), rowh);
    CFGSTC(IDC_STC_CFGHINT, L"", m + dpi_scale(52), y, dpi_scale(448), rowh * 2);
    y += rowh * 2 + dpi_scale(6);

    /* 按钮行：解锁编辑 + 密码框（点击解锁后才显示） + 保存 + 恢复默认 */
    CFGBTN(IDC_BTN_UNLOCK, L"解锁编辑", m, y, dpi_scale(90), rowh);
    CFGEDIT(IDC_CFG_EDIT_PASS, m + dpi_scale(100), y, dpi_scale(140), rowh,
            ES_PASSWORD | ES_AUTOHSCROLL);          /* 初始隐藏，点解锁后显示 */
    CFGBTN(IDC_BTN_SAVECFG, L"保存修改", m + dpi_scale(250), y, dpi_scale(90), rowh);
    CFGBTN(IDC_BTN_RESETCFG, L"恢复默认设置", m + dpi_scale(350), y, dpi_scale(110), rowh);
    y += rowh + dpi_scale(4);
    CFGSTC(IDC_STC_CFGSTATE, L"状态：只读（未授权）", m, y, dpi_scale(320), rowh);
    CFGBTN(IDC_BTN_CLOSECFG, L"关闭", m + dpi_scale(440), y, dpi_scale(60), rowh);
    y += rowh + dpi_scale(8);

    CFGSTC(0, L"配置内容：", m, y, dpi_scale(120), rowh);
    y += rowh;
    CFGEDIT(IDC_EDIT_CFGTEXT, m, y, dpi_scale(500), dpi_scale(250),
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY);
    #undef CFGSTC
    #undef CFGLIST
    #undef CFGEDIT
    #undef CFGCOMBO
    #undef CFGBTN
}

static LRESULT CALLBACK cfg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hcfg = h;
        cfg_create_controls(h);
        g_hcfglist = GetDlgItem(h, IDC_CFG_LIST_MODELS);
        g_hcfgtext = GetDlgItem(h, IDC_EDIT_CFGTEXT);
        g_hcfgpass = GetDlgItem(h, IDC_CFG_EDIT_PASS);
        g_hcfgstate = GetDlgItem(h, IDC_STC_CFGSTATE);
        g_hcfgkey = GetDlgItem(h, IDC_CFG_COMBO_KEY);
        g_hcfgval = GetDlgItem(h, IDC_EDIT_CFGVAL);
        g_hcfgvalcombo = GetDlgItem(h, IDC_CFG_COMBO_VAL);
        g_hcfghint = GetDlgItem(h, IDC_STC_CFGHINT);
        for (int i = 0; i < g_cfg.nmodels; i++)
            SendMessageW(g_hcfglist, LB_ADDSTRING, 0, (LPARAM)g_cfg.models[i].display);
        SendMessageW(g_hcfglist, LB_SETCURSEL, 0, 0);
        cfg_show_model();
        cfg_apply_lock();
        /* 密码输入框：平时隐藏且禁用，点击"解锁编辑"后才显示 */
        ShowWindow(g_hcfgpass, SW_HIDE);
        EnableWindow(g_hcfgpass, FALSE);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CFG_LIST_MODELS:
            if (HIWORD(wp) == LBN_SELCHANGE) cfg_show_model();
            break;
        case IDC_CFG_COMBO_KEY:
            if (HIWORD(wp) == CBN_SELCHANGE)
                cfg_sync_value_editor_ex(TRUE);    /* 列表选择：回填当前值 */
            else if (HIWORD(wp) == CBN_EDITCHANGE)
                cfg_sync_value_editor_ex(FALSE);   /* 手输键名：只切类型/更新说明 */
            break;
        case IDC_BTN_UNLOCK: {
            /* 两步式：第一次点击显示密码框；输入密码后再次点击校验 */
            if (!IsWindowVisible(g_hcfgpass)) {
                if (!g_cfg.adminpass[0]) {
                    MessageBoxW(h, L"ini 未设置管理员密码（ADMINPASS），暂不支持解锁编辑。",
                                L"提示", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                ShowWindow(g_hcfgpass, SW_SHOW);
                EnableWindow(g_hcfgpass, TRUE);
                SetWindowTextW(g_hcfgpass, L"");
                SetFocus(g_hcfgpass);
                SetWindowTextW(GetDlgItem(h, IDC_BTN_UNLOCK), L"确认解锁");
                break;
            }
            wchar_t pass[CFG_VAL_LEN];
            GetWindowTextW(g_hcfgpass, pass, CFG_VAL_LEN);
            if (wcscmp(pass, g_cfg.adminpass) == 0) {
                g_cfg_unlocked = TRUE;
                g_cfg_failcount = 0;
                cfg_apply_lock();
                /* 恢复密码框隐藏 */
                ShowWindow(g_hcfgpass, SW_HIDE);
                EnableWindow(g_hcfgpass, FALSE);
                SetWindowTextW(GetDlgItem(h, IDC_BTN_UNLOCK), L"解锁编辑");
                applog(CH_ENGINE, LOG_UI, L"[CFG] 配置窗口已解锁编辑");
            } else {
                g_cfg_failcount++;
                wchar_t msgb[128];
                _snwprintf(msgb, 128, L"密码错误（第 %d 次，连续 3 次锁定）", g_cfg_failcount);
                MessageBoxW(h, msgb, L"解锁失败", MB_OK | MB_ICONWARNING);
                if (g_cfg_failcount >= 3) {
                    ShowWindow(g_hcfgpass, SW_HIDE);
                    EnableWindow(g_hcfgpass, FALSE);
                    EnableWindow(GetDlgItem(h, IDC_BTN_UNLOCK), FALSE);
                    SetWindowTextW(GetDlgItem(h, IDC_BTN_UNLOCK), L"解锁编辑");
                    MessageBoxW(h, L"连续 3 次密码错误，密码输入已锁定。", L"锁定",
                                MB_OK | MB_ICONSTOP);
                }
            }
            break;
        }
        case IDC_BTN_SAVECFG: {
            if (!g_cfg_unlocked) {
                MessageBoxW(h, L"只读模式下不可修改配置。请先解锁。", L"提示",
                            MB_OK | MB_ICONINFORMATION);
                break;
            }
            wchar_t key[CFG_KEY_LEN] = L"", val[CFG_VAL_LEN] = L"";
            GetWindowTextW(g_hcfgkey, key, CFG_KEY_LEN);   /* 兼容手输的新键名 */
            if (!key[0]) {
                MessageBoxW(h, L"请选择要修改的键。", L"提示", MB_OK);
                break;
            }
            if (IsWindowVisible(g_hcfgvalcombo))
                GetWindowTextW(g_hcfgvalcombo, val, CFG_VAL_LEN);
            else
                GetWindowTextW(g_hcfgval, val, CFG_VAL_LEN);
            int sel = (int)SendMessageW(g_hcfglist, LB_GETCURSEL, 0, 0);
            if (sel < 0) break;
            wchar_t section[CFG_KEY_LEN];
            lstrcpynW(section, g_cfg.models[sel].internal, CFG_KEY_LEN);
            if (cfg_save_key(g_ini_path, section, key, val)) {
                /* 保存后立即重载配置并刷新主界面 */
                reload_config_and_ui();
                cfg_show_model();
                applog(CH_ENGINE, LOG_OK, L"[CFG] 已保存 %s.%s=%s（配置已重载）",
                       section, key, val);
            } else {
                MessageBoxW(h, L"保存失败（ini 写入错误）。", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        case IDC_BTN_RESETCFG: {
            if (MessageBoxW(h,
                    L"确认恢复默认设置？\n\n当前 UdiskTools.ini 将被出厂默认配置覆盖，\n自定义的机型与路径修改会丢失！",
                    L"恢复默认设置", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
                break;
            if (write_default_ini() && reload_config_and_ui()) {
                g_cfg_unlocked = FALSE;
                cfg_apply_lock();
                /* 刷新机型列表与内容展示 */
                SendMessageW(g_hcfglist, LB_RESETCONTENT, 0, 0);
                for (int i = 0; i < g_cfg.nmodels; i++)
                    SendMessageW(g_hcfglist, LB_ADDSTRING, 0, (LPARAM)g_cfg.models[i].display);
                SendMessageW(g_hcfglist, LB_SETCURSEL, 0, 0);
                cfg_show_model();
                applog(CH_ENGINE, LOG_UI, L"[CFG] 已恢复默认设置（内嵌出厂配置覆盖并重载）");
                MessageBoxW(h, L"已恢复默认设置并立即重载。", L"提示", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(h, L"恢复默认设置失败（ini 写入错误）。", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        case IDC_BTN_CLOSECFG:
            /* 保存/修改后关闭时也确保配置已重载 */
            reload_config_and_ui();
            DestroyWindow(h);
            break;
        }
        break;

    case WM_CLOSE:
        /* 关闭配置窗口时重载配置（覆盖外部修改并刷新主界面） */
        reload_config_and_ui();
        DestroyWindow(h);
        return 0;
    case WM_NCDESTROY:
        g_hcfg = NULL;
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static void open_config_dialog(void)
{
    if (g_hcfg && IsWindow(g_hcfg)) { SetForegroundWindow(g_hcfg); return; }
    g_cfg_unlocked = FALSE;
    g_cfg_failcount = 0;
    int w = dpi_scale(540), hgt = dpi_scale(620);
    CreateWindowExW(WS_EX_DLGMODALFRAME, L"UDK_CFGCLASS",
                    L"配置（只读，解锁后可编辑）",
                    (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) | WS_VISIBLE,
                    CW_USEDEFAULT, CW_USEDEFAULT, w, hgt,
                    g_hmain, NULL, g_hinst, NULL);
}

/* ============================================================
 * 主窗口过程
 * ============================================================ */

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_hmain = h;
        #define MKCTL(cls, ex, style, id, txt) \
            CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | (style), \
                            0, 0, 10, 10, h, (HMENU)(INT_PTR)(id), g_hinst, NULL)
        g_hmodel = MKCTL(L"COMBOBOX", 0, CBS_DROPDOWNLIST | WS_VSCROLL, IDC_COMBO_MODEL, L"");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_START, L"开始");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_STOP, L"停止");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_FORMAT, L"格式化");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_READLABEL, L"读取标签");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_WRITELABEL, L"复制");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_READCHECK, L"读取校验");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_RWTEST, L"读写测试");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_CONFIG, L"配置");
        g_hchk_top = MKCTL(L"BUTTON", 0, BS_AUTOCHECKBOX, IDC_CHK_TOPMOST, L"窗口置顶");
        g_hchk_hotkey = MKCTL(L"BUTTON", 0, BS_AUTOCHECKBOX, IDC_CHK_HOTKEY, L"快捷键使能");
        g_hlabel = MKCTL(L"STATIC", 0, SS_LEFT, IDC_STC_LABEL, L"标签：");
        g_hautomode = MKCTL(L"RICHEDIT50W", 0, ES_READONLY | ES_CENTER, IDC_STC_AUTOMODE, L"MANUAL");
        g_hsrcpath = MKCTL(L"STATIC", 0, SS_LEFT | SS_ENDELLIPSIS, IDC_STC_SRCPATH, L"SRCPATH：");
        g_hcount = MKCTL(L"STATIC", 0, SS_RIGHT, IDC_STC_INSERTCOUNT, L"计数:0000");
        g_hdev = MKCTL(L"STATIC", 0, SS_LEFT, IDC_STC_DEVICE, L"设备状态：检测中…");
        g_herr = MKCTL(L"RICHEDIT50W", 0, ES_READONLY, IDC_STC_ERROR, L"");
        g_hprog = MKCTL(PROGRESS_CLASSW, 0, PBS_SMOOTH, IDC_PBAR_TASK, L"");
        g_hprogtitle = MKCTL(L"STATIC", 0, SS_LEFT, IDC_STC_PROGTITLE, L"任务进度：");
        g_hprogpct = MKCTL(L"STATIC", 0, SS_RIGHT, IDC_STC_PROGPCT, L"0%");

        /* 底部监控窗格 */
        g_hmonstatus = MKCTL(L"RICHEDIT50W", 0, ES_READONLY | ES_CENTER, IDC_STC_MONSTATUS, L"监控已停止");
        g_hmonfile = MKCTL(L"STATIC", 0, SS_CENTER, IDC_STC_MONFILE, L"");
        g_hmonbtn[0] = MKCTL(L"BUTTON", 0, BS_OWNERDRAW, IDC_BTN_MONSTART, L"开始监控");
        g_hmonbtn[1] = MKCTL(L"BUTTON", 0, BS_OWNERDRAW, IDC_BTN_MONSTOP, L"停止监控");
        g_hmonbtn[2] = MKCTL(L"BUTTON", 0, BS_OWNERDRAW, IDC_BTN_EXIT, L"退出");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_CLEARCMD, L"清屏");
        g_hcmd[0] = MKCTL(L"RICHEDIT50W", WS_EX_CLIENTEDGE,
                          ES_MULTILINE | ES_READONLY | WS_VSCROLL, IDC_EDIT_ENGINE, L"");
        g_hcmd[1] = MKCTL(L"RICHEDIT50W", WS_EX_CLIENTEDGE,
                          ES_MULTILINE | ES_READONLY | WS_VSCROLL, IDC_EDIT_IDENT, L"");
        g_hverdict = MKCTL(L"RICHEDIT50W", 0, ES_READONLY | ES_CENTER, IDC_STC_READVERDICT, L"");
        g_hfile[0] = MKCTL(L"RICHEDIT50W", WS_EX_CLIENTEDGE,
                           ES_MULTILINE | ES_READONLY | WS_VSCROLL, IDC_EDIT_FILE, L"");
        g_hfile[1] = MKCTL(L"RICHEDIT50W", WS_EX_CLIENTEDGE,
                           ES_MULTILINE | ES_READONLY | WS_VSCROLL, IDC_EDIT_FILE2, L"");
        #undef MKCTL
        return 0;
    }

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        dpi_update_font();
        RECT* prc = (RECT*)lp;
        SetWindowPos(h, NULL, prc->left, prc->top,
                     prc->right - prc->left, prc->bottom - prc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_SIZE:
        layout();
        return 0;

    case WM_DRAWITEM: {
        /* 自绘监控按钮：按状态变色（禁用=浅灰，开始=绿，停止=红，退出=灰） */
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lp;
        int idx = -1;
        if (dis->CtlID == IDC_BTN_MONSTART) idx = 0;
        else if (dis->CtlID == IDC_BTN_MONSTOP) idx = 1;
        else if (dis->CtlID == IDC_BTN_EXIT) idx = 2;
        if (idx < 0) break;
        BOOL disabled = (dis->itemState & ODS_DISABLED) != 0;
        COLORREF bg = disabled ? RGB(205, 205, 205) : g_btncol[idx];
        COLORREF fg = disabled ? RGB(130, 130, 130) : RGB(255, 255, 255);
        HBRUSH br = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, br);
        DeleteObject(br);
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, fg);
        wchar_t txt[64];
        GetWindowTextW(dis->hwndItem, txt, 64);
        HFONT of = (HFONT)SelectObject(dis->hDC, g_font);
        DrawTextW(dis->hDC, txt, -1, &dis->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, of);
        return TRUE;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        switch (id) {
        case IDC_COMBO_MODEL:
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(g_hmodel, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < g_cfg.nmodels && sel != g_cur_model) {
                    /* 切换机型：立即从磁盘重载配置项，再定位到所选机型 */
                    wchar_t disp[CFG_VAL_LEN];
                    SendMessageW(g_hmodel, CB_GETLBTEXT, sel, (LPARAM)disp);
                    if (reload_config_and_ui()) {
                        int newsel = -1;
                        for (int i = 0; i < g_cfg.nmodels; i++)
                            if (wcscmp(g_cfg.models[i].display, disp) == 0) { newsel = i; break; }
                        if (newsel < 0) newsel = 0;
                        g_cur_model = newsel;
                        SendMessageW(g_hmodel, CB_SETCURSEL, newsel, 0);
                    }
                    applog(CH_ENGINE, LOG_UI, L"[UI] 机型切换为：%s（内部键 %s，配置已重载，与轮询解耦）",
                           g_cfg.models[g_cur_model].display, g_cfg.models[g_cur_model].internal);
                    refresh_buttons();
                }
            }
            break;
        case IDC_BTN_START:
            task_run_async(g_cur_model, TRIGGER_MANUAL);
            break;
        case IDC_BTN_STOP:
            InterlockedExchange(&g_stop, 1);
            applog(CH_ENGINE, LOG_UI, L"[UI] 操作员请求停止");
            break;
        case IDC_BTN_FORMAT:
            run_single(OP_FORMAT);
            break;
        case IDC_BTN_READLABEL:
            run_single(OP_LABELREAD);
            break;
        case IDC_BTN_WRITELABEL:
            run_single(OP_COPY);   /* 专职复制：成功绿 Copy Complete! / 失败红 Copy Failure! */
            break;
        case IDC_BTN_READCHECK:
            run_single(OP_READ);   /* 读取 + READRULE 判定：PASS 绿 / FAIL 红 */
            break;
        case IDC_BTN_RWTEST:
            run_single(OP_RWTEST); /* 读写测试：写读 MD5 比对 + 速率 */
            break;
        case IDC_BTN_CONFIG:
            open_config_dialog();
            break;
        case IDC_BTN_MONSTART:
            if (code == BN_CLICKED) {
                InterlockedExchange(&g_monitoring, 1);
                set_monstatus(L"监控已开启：等待插入可移动磁盘…", L"", RGB(0, 150, 0));
                applog(CH_ENGINE, LOG_UI, L"[UI] 操作员开启监控（插盘自动执行当前机型任务）");
                refresh_buttons();
            }
            break;
        case IDC_BTN_MONSTOP:
            if (code == BN_CLICKED) {
                InterlockedExchange(&g_monitoring, 0);
                /* 仅在任务执行中才置停止标志；设备轮询/状态显示不受监控开关影响 */
                if (g_running) InterlockedExchange(&g_stop, 1);
                set_monstatus(L"监控已停止", L"", RGB(200, 0, 0));
                applog(CH_ENGINE, LOG_UI, L"[UI] 操作员停止监控（设备检测仍持续显示插拔信息）");
                refresh_buttons();
            }
            break;
        case IDC_BTN_CLEARCMD:
            if (code == BN_CLICKED) {
                SetWindowTextW(g_hcmd[0], L"");
                applog(CH_ENGINE, LOG_UI, L"[UI] CMD-A 已清屏");
            }
            break;
        case IDC_BTN_EXIT:
            if (code == BN_CLICKED) PostMessageW(h, WM_CLOSE, 0, 0);
            break;
        case IDC_CHK_TOPMOST:
            if (code == BN_CLICKED) {
                BOOL on = SendMessageW(g_hchk_top, BM_GETCHECK, 0, 0) == BST_CHECKED;
                SetWindowPos(h, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                applog(CH_ENGINE, LOG_UI, L"[UI] 窗口置顶 %s（%s）",
                       on ? L"开启" : L"关闭", on ? L"HWND_TOPMOST" : L"HWND_NOTOPMOST");
            }
            break;
        case IDC_CHK_HOTKEY:
            if (code == BN_CLICKED) {
                BOOL on = SendMessageW(g_hchk_hotkey, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (on) {
                    RegisterHotKey(h, ID_HOTKEY_T, 0, 'T');
                    RegisterHotKey(h, ID_HOTKEY_C, 0, 'C');
                } else {
                    UnregisterHotKey(h, ID_HOTKEY_T);
                    UnregisterHotKey(h, ID_HOTKEY_C);
                }
                g_hotkey_enabled = on;
                applog(CH_ENGINE, LOG_UI, L"[UI] 快捷键使能 %s（T/C 全局热键%s）",
                       on ? L"开启" : L"关闭", on ? L"已注册" : L"已注销");
            }
            break;
        }
        return 0;
    }

    case WM_HOTKEY:
        if (wp == ID_HOTKEY_T) {
            applog(CH_ENGINE, LOG_UI, L"[UI] 热键 T：执行任务");
            task_run_async(g_cur_model, TRIGGER_MANUAL);
        }
        if (wp == ID_HOTKEY_C) {
            applog(CH_ENGINE, LOG_UI, L"[UI] 热键 C：执行任务");
            task_run_async(g_cur_model, TRIGGER_MANUAL);
        }
        return 0;

    case WM_APP_LOG: {
        int ch = (int)(wp & 0xFFFF);
        int lv = (int)(wp >> 16);
        con_append(g_hcmd[(ch >= 0 && ch < 2) ? ch : 0], (wchar_t*)lp, (LOGLEVEL)lv);
        free((void*)lp);
        return 0;
    }
    case WM_APP_PROGRESS: {
        SendMessageW(g_hprog, PBM_SETPOS, wp, 0);
        wchar_t pt[16];
        _snwprintf(pt, 16, L"%d%%", (int)wp);
        SetWindowTextW(g_hprogpct, pt);
        return 0;
    }
    case WM_APP_DEVCOUNT: {
        DWORD mask = (DWORD)wp;
        BOOL inserted = lp != 0;
        DWORD changed = inserted ? (mask & ~g_dev_mask) : (g_dev_mask & ~mask);
        g_dev_mask = mask;
        update_device_text();
        for (wchar_t d = L'C'; d <= L'Z'; d++) {
            if (changed & (1u << (d - L'A'))) {
            if (inserted) {
                applog(CH_ENGINE, LOG_UI, L"[DEV] 检测到可移动磁盘 %c:\\ 插入", d);
                /* 每次插盘先清掉上一轮的信息状态：出错徽标熄灭 + CMD-A 清屏，本轮日志从头显示 */
                set_error_badge(FALSE, NULL);
                SetWindowTextW(g_hcmd[0], L"");
                set_ok_badge(L"");   /* 绿色正常提示（后续任务出错会转红） */
                /* 进度条与文件内容窗格同步复位，本轮从零显示 */
                SendMessageW(g_hprog, PBM_SETPOS, 0, 0);
                SetWindowTextW(g_hprogpct, L"0%");
                SetWindowTextW(g_hfile[0], L"");
                SetWindowTextW(g_hfile[1], L"");
                /* 插拔计数器：仅监控开启时计数；4 位循环（9999→0000），退出程序不保存 */
                if (g_monitoring) {
                    InterlockedIncrement(&g_inscount);
                    update_count_text();
                }
                /* 机型 = 当前下拉框选择（与轮询解耦）；监控开启或机型 AUTORUN=1 时自动执行 */
                    CFGMODEL* m = &g_cfg.models[g_cur_model];
                    if ((g_monitoring || cfg_get_int(m, L"AUTORUN", 0)) && !g_running)
                        task_run_async(g_cur_model, TRIGGER_AUTO);
                } else {
                    applog(CH_ENGINE, LOG_UI, L"[DEV] 可移动磁盘 %c:\\ 已拔出", d);
                    SendMessageW(g_hprog, PBM_SETPOS, 0, 0);
                }
            }
        }
        return 0;
    }
    case WM_APP_VERDICT: {
        /* wparam: 0=PASS/FAIL 逻辑配色；1=绿色文本；2=红色文本 */
        wchar_t* s = (wchar_t*)lp;
        if (!s) return 0;
        if (wp == 1)      set_verdict_colored(s, RGB(0, 150, 0));
        else if (wp == 2) set_verdict_colored(s, RGB(200, 0, 0));
        else              set_verdict(s);
        free(s);
        return 0;
    }
    case WM_APP_FILECONTENT: {
        int pane = (wp == 1) ? 1 : 0;
        SetWindowTextW(g_hfile[pane], (wchar_t*)lp ? (wchar_t*)lp : L"");
        free((void*)lp);
        return 0;
    }
    case WM_APP_LABEL: {
        wchar_t txt[600];
        _snwprintf(txt, 600, L"标签：%s", (wchar_t*)lp ? (wchar_t*)lp : L"");
        SetWindowTextW(g_hlabel, txt);
        free((void*)lp);
        return 0;
    }
    case WM_APP_ERROR: {
        /* wparam: 1=红色出错；0=绿色正常提示（有文本时） */
        wchar_t* s = (wchar_t*)lp;
        if (wp != 0) set_error_badge(TRUE, s);
        else if (s && s[0]) set_ok_badge(s);
        else set_error_badge(FALSE, NULL);
        free((void*)s);
        return 0;
    }
    case WM_APP_MODE:
        /* 已废弃（待机/模式标记取消），保留消息号兼容 */
        return 0;
    case WM_APP_MONINFO: {
        wchar_t* s = (wchar_t*)lp;
        if (s) {
            wchar_t* nl = wcschr(s, L'\n');
            if (nl) { *nl = 0; set_monstatus(s, nl + 1, RGB(60, 60, 60)); }
            else set_monstatus(s, L"", RGB(60, 60, 60));
            free(s);
        }
        return 0;
    }
    case WM_APP_TASKDONE:
        InterlockedExchange(&g_running, 0);
        InterlockedExchange(&g_stop, 0);
        /* 徽标随结果刷新：成功=绿"任务完成"；已停止=绿"任务已停止"；失败时红色出错徽标已由 ops 置位 */
        if (wp == 0)      set_ok_badge(L"任务完成");
        else if (wp == 2) set_ok_badge(L"任务已停止");
        refresh_buttons();
        return 0;

    case WM_CLOSE:
        if (g_running) {
            if (MessageBoxW(h, L"任务正在执行，确认退出？", L"退出确认",
                            MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
                return 0;
        }
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        dev_poll_stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ---------- GUI 主循环 ---------- */

int gui_run(HINSTANCE hInst, int nShow)
{
    g_hinst = hInst;
    dpi_init();

    HMODULE me = LoadLibraryW(L"Msftedit.dll");   /* RICHEDIT50W 类 */
    if (!me) LoadLibraryW(L"Riched20.dll");

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"UDK_MAINCLASS";
    RegisterClassW(&wc);

    WNDCLASSW wc2;
    memset(&wc2, 0, sizeof(wc2));
    wc2.lpfnWndProc = cfg_proc;
    wc2.hInstance = hInst;
    wc2.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc2.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc2.lpszClassName = L"UDK_CFGCLASS";
    wc2.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc2);

    g_dpi = get_system_dpi();

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int W = dpi_scale(880), H = dpi_scale(760);   /* 纵向加高：文件内容区不滚动可显示 10 行以上 */
    int x = wa.right > W ? (wa.right - W) / 2 : CW_USEDEFAULT;
    int y2 = wa.bottom > H ? (wa.bottom - H) / 3 : CW_USEDEFAULT;

    wchar_t title[300];
    _snwprintf(title, 300, L"%s v%s", g_cfg.title, g_cfg.version);
    HWND h = CreateWindowExW(0, L"UDK_MAINCLASS", title,
                             WS_OVERLAPPEDWINDOW,
                             x, y2, W, H, NULL, NULL, hInst, NULL);
    if (!h) return -1;

    dpi_update_font();

    /* 机型下拉框（显示名） */
    for (int i = 0; i < g_cfg.nmodels; i++)
        SendMessageW(g_hmodel, CB_ADDSTRING, 0, (LPARAM)g_cfg.models[i].display);
    g_cur_model = 0;
    for (int i = 0; i < g_cfg.nmodels; i++)
        if (_wcsicmp(g_cfg.models[i].internal, g_cfg.defaultmodel) == 0)
            g_cur_model = i;
    SendMessageW(g_hmodel, CB_SETCURSEL, g_cur_model, 0);

    log_set_gui_hwnd(h);
    ops_set_result_target(h);

    ShowWindow(h, nShow);
    UpdateWindow(h);
    layout();
    refresh_buttons();
    refresh_run_state();   /* 启动时按配置推导：本机型 AUTORUN=1 直接显示 AUTO */
    set_monstatus(L"监控已停止", L"", RGB(200, 0, 0));   /* 初始状态：红色 */

    /* 快捷键默认不注册，由"快捷键使能"复选框控制 */
    if (g_hotkey_enabled) {
        RegisterHotKey(h, ID_HOTKEY_T, 0, 'T');
        RegisterHotKey(h, ID_HOTKEY_C, 0, 'C');
    }

    applog(CH_ENGINE, LOG_UI, L"[SYS] %s v%s 启动（ini：%s）",
           g_cfg.title, g_cfg.version, g_ini_path);
    applog(CH_ENGINE, LOG_UI, L"[SYS] 已加载 %d 个机型，当前默认：%s",
           g_cfg.nmodels, g_cfg.models[g_cur_model].internal);
    if (!g_cfg.adminpass[0])
        applog(CH_ENGINE, LOG_WARN, L"[SYS] 未设置管理员密码，配置窗口仅只读");

    /* 启动设备轮询（机型匹配与轮询解耦） */
    dev_poll_start(h);
    /* 启动时先枚举一次：仅刷新设备/标签行显示（已插着的盘不计数、不触发任务） */
    dev_enum_removable(&g_dev_mask, NULL);
    update_device_text();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterHotKey(h, ID_HOTKEY_T);
    UnregisterHotKey(h, ID_HOTKEY_C);
    if (me) FreeLibrary(me);
    return (int)msg.wParam;
}
