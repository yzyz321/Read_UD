/* ============================================================
 * gui.c - 主窗口（WND_MAIN / UDK_MAINCLASS）
 * 职责：控件创建/布局、DPI（Per-Monitor V2）、设备轮询事件、
 *       任务/单操作编排（worker 线程 + WM_APP_* 消息）、
 *       配置重载与标题/按钮刷新、全局热键、退出等待。
 * 拆分：配置窗口（UDK_CFGCLASS）在 gui_config.c；
 *       两个文件共享符号见 gui_shared.h（勿在 gui.h 暴露内部符号）。
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gui.h"
#include "gui_shared.h"
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
static HANDLE g_worker = NULL;      /* 当前工作线程句柄（WM_APP_TASKDONE 时 join 后关闭） */
static BOOL g_closing = FALSE;      /* 退出流程中：忽略新的界面命令/重入 WM_CLOSE */
static int g_exit_code = 0;         /* GUI 退出码：0=正常 / 1=GetMessage 失败 / 3=强制退出（P3-6） */

HINSTANCE g_hinst = NULL;
HWND g_hmain = NULL;
static HWND g_hmodel = NULL, g_hlabel = NULL, g_hdev = NULL,
            g_herr = NULL, g_hprog = NULL, g_hcmd[2] = { NULL, NULL },
            g_hverdict = NULL, g_hfile[2] = { NULL, NULL }, g_hchk_top = NULL,
            g_hchk_autocopy = NULL, g_hchk_autotest = NULL,
            g_hchk_hotkey = NULL, g_hautomode = NULL, g_hsrcpath = NULL,
            g_hcount = NULL, g_hprogtitle = NULL, g_hprogpct = NULL,
            g_hreadrule = NULL;
static volatile LONG g_inscount = 0;    /* 插拔计数器：每次插入 +1（不持久化，退出即清零） */
static BOOL g_hotkey_enabled = FALSE;   /* 快捷键使能（IDC_CHK_HOTKEY） */
HFONT g_font = NULL, g_font_mono = NULL, g_font_bold = NULL;
static UINT g_dpi = 96;
static DWORD g_dev_mask = 0;
static wchar_t g_pending_drive = 0;  /* 任务执行中插入的盘：任务结束后补触发自动任务（0=无） */
int g_cur_model = 0;         /* 当前机型索引（与轮询解耦） */
static int g_ident_index = 0;       /* 标识生成计数 */
static int g_single_op = -1;        /* >=0=正在执行/刚完成的单操作（OP_*），-1=整任务；用于完成文案 */
static const wchar_t* g_riched_class = L"RICHEDIT50W";  /* P2-7：Msftedit 失败→RICHEDIT20W→STATIC */
static BOOL g_riched_ok = TRUE;     /* 文本控件是否为 RichEdit（可安全使用 EM_* 富文本消息） */

/* ---------- 监控窗格（PANE_BOTTOM） ---------- */
static volatile LONG g_monitoring = 0;              /* 监控开关 */
static HWND g_hmonbtn[3] = { NULL, NULL, NULL };    /* 开始/停止/退出 */
static HWND g_hmonstatus = NULL, g_hmonfile = NULL, g_hmonfile2 = NULL;
/* 配置窗口句柄（配置窗口段使用；字体刷新 dpi_update_font 需提前引用） */
HWND g_hcfg = NULL;      /* 定义在此，gui_config.c 经 gui_shared.h 共享 */
/* P3-1：IDC_STC_MONFILE/1108 是 STATIC，颜色不能再用 EM_SETCHARFORMAT，
 * 改由 WM_CTLCOLORSTATIC 根据这里的颜色绘制。 */
static COLORREF g_monfile_col = RGB(0, 90, 200), g_monfile2_col = RGB(0, 90, 200);
static const COLORREF g_btncol[3] = {
    RGB(0, 150, 0),     /* 开始监控：绿 */
    RGB(210, 0, 0),     /* 停止监控：红 */
    RGB(170, 170, 170), /* 退出：灰 */
};

/* ---------- 前向声明 ---------- */
static void update_srcpath_text(void);
static void set_run_state(BOOL is_auto);
static void refresh_run_state(void);
static void update_auto_flags_ui(void);
static void update_readrule_text(void);

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

int dpi_scale(int v) { return MulDiv(v, (int)g_dpi, 96); }

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

/* 按当前 DPI 重建字体：先创建新字体并替换所有控件字体，再删除旧字体，
 * 避免控件仍持有已删除的 HFONT（P2-5 悬空字体）。
 * 配置窗口是独立顶层窗口（不在 EnumChildWindows(g_hmain) 范围内），需单独广播。 */
void dpi_update_font(void)
{
    int h1 = -MulDiv(9, (int)g_dpi, 72);
    HFONT nf = CreateFontW(h1, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT nm = CreateFontW(h1, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                           0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    HFONT nb = CreateFontW(h1, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                           0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT of = g_font, om = g_font_mono, ob = g_font_bold;
    if (nf) g_font = nf;
    if (nm) g_font_mono = nm;
    if (nb) g_font_bold = nb;

    if (g_hmain) {
        EnumChildWindows(g_hmain, font_enum, (LPARAM)g_font);
        if (g_hcmd[0]) SendMessageW(g_hcmd[0], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hcmd[1]) SendMessageW(g_hcmd[1], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hfile[0]) SendMessageW(g_hfile[0], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hfile[1]) SendMessageW(g_hfile[1], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hdev)    SendMessageW(g_hdev, WM_SETFONT, (WPARAM)g_font_bold, TRUE);  /* 设备状态粗体 */
        if (g_hcount)  SendMessageW(g_hcount, WM_SETFONT, (WPARAM)g_font_bold, TRUE); /* 插拔计数粗体 */
    }
    if (g_hcfg && IsWindow(g_hcfg))
        EnumChildWindows(g_hcfg, font_enum, (LPARAM)g_font);

    /* 旧字体已无控件引用，安全删除（创建失败时保留旧字体） */
    if (of && of != g_font) DeleteObject(of);
    if (om && om != g_font_mono) DeleteObject(om);
    if (ob && ob != g_font_bold) DeleteObject(ob);
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
    if (!g_riched_ok) return;   /* P2-7：RichEdit 完全不可用时（STATIC 回退）不做 EM_* 富文本操作 */
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

void refresh_buttons(void)
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
    update_auto_flags_ui();  /* F2：AUTO_COPY/AUTO_TEST 只读状态框随机型同步 */
    update_readrule_text();  /* F3：AUTO_TEST=1 时显示 READRULE 判定规则 */
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
    if (!h) return;
    SetWindowTextW(h, txt);
    if (g_riched_ok) {
        CHARFORMAT2W cf; memset(&cf, 0, sizeof(cf));
        cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR | CFM_BOLD;
        cf.dwEffects = CFE_BOLD; cf.crTextColor = color;
        SendMessageW(h, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
    }
    /* P2-4：标题可能很长（TITLE+VERSION 各 ≤511），缓冲需足够且始终 NUL */
    wchar_t t[1200] = L"";
    GetWindowTextW(g_hmain, t, 1200);
    wchar_t* bang = wcsstr(t, L" [!]");
    if (bang) { *bang = 0; SetWindowTextW(g_hmain, t); }
    if (is_error) {
        GetWindowTextW(g_hmain, t, 1200);
        if (!wcsstr(t, L"[!]")) {
            wchar_t t2[1220];
            _snwprintf(t2, 1220, L"%s [!]", t);
            t2[1219] = 0;
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
        txt[299] = 0;
        badge_paint(txt, RGB(200, 0, 0), TRUE);
    } else {
        SetWindowTextW(g_herr, L"");
        wchar_t t[1200] = L"";
        GetWindowTextW(g_hmain, t, 1200);
        wchar_t* bang = wcsstr(t, L" [!]");
        if (bang) { *bang = 0; SetWindowTextW(g_hmain, t); }
    }
}

static void set_text_color(HWND h, const wchar_t* txt, COLORREF color)
{
    if (!h) return;
    SetWindowTextW(h, txt);
    if (!g_riched_ok) return;   /* STATIC 回退：无富文本能力，仅显示文本 */
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD;
    cf.dwEffects = CFE_BOLD;
    cf.crTextColor = color;
    SendMessageW(h, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

/* P3-1：STATIC 控件文字颜色——记录颜色后触发 WM_CTLCOLORSTATIC 重绘。
 * （EM_SETCHARFORMAT 对 STATIC 无效；g_hmonfile/g_hmonfile2 用本函数） */
static void set_static_text(HWND h, const wchar_t* text, COLORREF color)
{
    if (!h) return;
    if (h == g_hmonfile)       g_monfile_col = color;
    else if (h == g_hmonfile2) g_monfile2_col = color;
    SetWindowTextW(h, text ? text : L"");
    InvalidateRect(h, NULL, TRUE);
}

/* P2-2：进度条与百分比文本的唯一来源（统一 0..100，越界截断）。
 * 任务开始/结束/停止/拔出以及 WM_APP_PROGRESS 全部只经本函数更新，
 * ops 侧的字节进度/文件计数/格式化进度都先换算成 0..100 再投递。 */
static void post_progress_pct(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (g_hprog) SendMessageW(g_hprog, PBM_SETPOS, (WPARAM)pct, 0);
    if (g_hprogpct) {
        wchar_t pt[16];
        _snwprintf(pt, 16, L"%d%%", pct);
        pt[15] = 0;
        SetWindowTextW(g_hprogpct, pt);
    }
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

/* F2：AUTO_COPY/AUTO_TEST 只读状态框（灰色禁用）——显示当前机型的自动流程配置。
 * 默认值与 worker 逻辑保持一致（task_thread：AUTO_COPY 默认 1、AUTO_TEST 默认 0）。 */
static void update_auto_flags_ui(void)
{
    if (g_cfg.nmodels <= 0 || g_cur_model < 0 || g_cur_model >= g_cfg.nmodels) return;
    const CFGMODEL* m = &g_cfg.models[g_cur_model];
    int autocopy = cfg_get_int(m, L"AUTO_COPY", 1);
    int autotest = cfg_get_int(m, L"AUTO_TEST", 0);
    if (g_hchk_autocopy) {
        SendMessageW(g_hchk_autocopy, BM_SETCHECK,
                     autocopy ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(g_hchk_autocopy, FALSE);   /* 只读状态显示：保持灰色禁用 */
    }
    if (g_hchk_autotest) {
        SendMessageW(g_hchk_autotest, BM_SETCHECK,
                     autotest ? BST_CHECKED : BST_UNCHECKED, 0);
        EnableWindow(g_hchk_autotest, FALSE);
    }
}

/* F3：READRULE 判定规则标签栏——本机型 AUTO_TEST=1（自动流程会执行读写测试）时显示规则；
 * AUTO_TEST=0 时置空，避免无关信息干扰。仅 UI 展示，不改变 ops 的判定语义。 */
static void update_readrule_text(void)
{
    if (!g_hreadrule) return;
    if (g_cfg.nmodels <= 0 || g_cur_model < 0 || g_cur_model >= g_cfg.nmodels) {
        SetWindowTextW(g_hreadrule, L"");
        return;
    }
    const CFGMODEL* m = &g_cfg.models[g_cur_model];
    if (cfg_get_int(m, L"AUTO_TEST", 0) == 0) {
        SetWindowTextW(g_hreadrule, L"");
        return;
    }
    const wchar_t* rule = cfg_get(m, L"READRULE");
    wchar_t txt[CFG_VAL_LEN + 32];
    if (rule && rule[0])
        _snwprintf(txt, CFG_VAL_LEN + 32, L"判定规则：%s", rule);
    else
        lstrcpynW(txt, L"判定规则：（未配置 READRULE）", CFG_VAL_LEN + 32);
    txt[CFG_VAL_LEN + 31] = 0;
    SetWindowTextW(g_hreadrule, txt);
}

/* 监控状态行：标题按颜色显示（停止=红 / 开启=绿 / 命中=黑）+ 副行蓝
 * P3-1：副行 g_hmonfile 是 STATIC，颜色走 set_static_text/WM_CTLCOLORSTATIC。 */
static void set_monstatus(const wchar_t* title, const wchar_t* sub, COLORREF titlecol)
{
    set_text_color(g_hmonstatus, title ? title : L"", titlecol);
    if (sub && sub[0])
        set_static_text(g_hmonfile, sub, RGB(0, 90, 200));
    else
        set_static_text(g_hmonfile, L"", RGB(0, 90, 200));
}

/* P2-1：任务进行中 / 空闲时的监控状态行（任务开始与结束统一使用） */
static void set_monstatus_task_running(void)
{
    set_monstatus(g_monitoring ? L"监控中：任务执行中…" : L"任务执行中…",
                  L"", RGB(60, 60, 60));
}

static void set_monstatus_idle(void)
{
    if (g_monitoring)
        set_monstatus(L"监控已开启：等待插入可移动磁盘…", L"", RGB(0, 150, 0));
    else
        set_monstatus(L"监控已停止", L"", RGB(200, 0, 0));
}

/* 插拔计数器：仅在监控开启时计数；显示"计数:0000"4 位循环（9999 后回 0000） */
static void update_count_text(void)
{
    if (!g_hcount) return;
    wchar_t ct[32];
    _snwprintf(ct, 32, L"计数:%04lu", (unsigned long)(g_inscount % 10000));
    SetWindowTextW(g_hcount, ct);
}

/* P2-8：有界追加（保证 NUL 结尾、绝不越界），用于多盘/长卷标的设备行 */
static void wbuf_append(wchar_t* buf, size_t cap, size_t* len, const wchar_t* s)
{
    if (!buf || cap == 0 || !len || !s) return;
    if (*len >= cap - 1) { buf[cap - 1] = 0; return; }
    size_t room = cap - 1 - *len;
    size_t n = wcslen(s);
    if (n > room) n = room;   /* 超长时截断但不越界 */
    memcpy(buf + *len, s, n * sizeof(wchar_t));
    *len += n;
    buf[*len] = 0;
}

static void update_device_text(void)
{
    if (!g_dev_mask) {
        SetWindowTextW(g_hdev, L"设备状态：未检测到可移动磁盘（等待插入…）");
        SetWindowTextW(g_hlabel, L"标签：（无磁盘）");
        return;
    }
    /* 26 盘 × 长卷标（DEVINFO.volume 最多 128 字符）也放得下，且有界追加防溢出 */
    wchar_t txt[8192], lbl[8192];
    size_t tl = 0, ll = 0;
    lstrcpynW(txt, L"设备状态：", 8192);
    lstrcpynW(lbl, L"标签：", 8192);
    tl = wcslen(txt);
    ll = wcslen(lbl);
    BOOL any = FALSE;
    for (wchar_t d = L'C'; d <= L'Z'; d++) {
        if (g_dev_mask & (1u << (d - L'A'))) {
            DEVINFO di;
            memset(&di, 0, sizeof(di));
            /* P2-23：UI 线程只取卷信息（dev_get_info_volume 不跑 SetupAPI），
             * 避免每次插拔/刷新在界面线程做 USB 父链枚举造成卡顿。
             * 注意：该 helper 不填 vidpid/usbserial（恒为空），因此本行标签
             * 只显示卷标；USB VID/SN 仍由任务线程的 dev_capture_sig 采集使用。 */
            dev_get_info_volume(d, &di);
            wchar_t one[320];
            _snwprintf(one, 320, L"%c: [%s|%.1fGB]  ", d,
                       di.volume[0] ? di.volume : L"无卷标",
                       di.total_bytes / 1073741824.0);
            one[319] = 0;
            wbuf_append(txt, 8192, &tl, one);
            if (di.volume[0]) {
                if (any) wbuf_append(lbl, 8192, &ll, L"  ");
                _snwprintf(one, 320, L"%c: %s", d, di.volume);
                one[319] = 0;
                wbuf_append(lbl, 8192, &ll, one);
            }
            /* USB 标识（di.vidpid/usbserial）不再在 UI 线程采集；若将来改为
             * 由设备事件线程缓存后再显示，可在此拼接缓存值。 */
            any = TRUE;
        }
    }
    if (!any) wbuf_append(lbl, 8192, &ll, L"（当前磁盘均无卷标）");
    SetWindowTextW(g_hdev, txt);
    SetWindowTextW(g_hlabel, lbl);   /* IDC_STC_LABEL 1101 随插拔实时刷新 */
}

/* P2-1：统一的“任务界面”状态复位。
 *   new_session=TRUE  ：新盘会话/拔出——清判定、文件双窗格、命中文件行、进度、错误徽标，
 *                       并恢复绿色“✓ 正常”；保留 CMD-A/CMD-B 日志与插拔计数（batch2 行为）。
 *   new_session=FALSE ：任务开始前——清旧判定/内容/命中行与错误徽标，进度归零；不清日志。 */
static void reset_task_ui(BOOL new_session)
{
    if (g_hverdict) SetWindowTextW(g_hverdict, L"");
    if (g_hfile[0])  SetWindowTextW(g_hfile[0], L"");
    if (g_hfile[1])  SetWindowTextW(g_hfile[1], L"");
    set_static_text(g_hmonfile, L"", RGB(0, 90, 200));
    set_static_text(g_hmonfile2, L"", RGB(0, 90, 200));
    post_progress_pct(0);
    set_error_badge(FALSE, NULL);
    if (new_session) set_ok_badge(L"");
}

/* 主窗口标题：TITLE + 软件版本 + 配置版本（"TITLE（软件 vX.Y.Z）（配置 vA.B.C）"；
 * 配置版本为空时省略"（配置 v…）"）。
 * 注意区分：g_cfg.version 是“配置文件版本”（[TOOLSINFO] VERSION，可为空）；
 *           APP_VERSION_W 是“软件/程序版本”（src/version.h，构建时固定）。
 * gui_run 初始创建窗口与 reload_config_and_ui 成功后都调用它（L14：标题不刷新）。
 * P2-4：TITLE/VERSION 各最长 CFG_VAL_LEN-1=511，1200 字符缓冲足够不截断。 */
void update_main_title(void)
{
    if (!g_hmain) return;
    wchar_t title[1200];
    if (g_cfg.version[0])
        _snwprintf(title, 1200, L"%s（软件 v%s）（配置 v%s）",
                   g_cfg.title, APP_VERSION_W, g_cfg.version);
    else
        _snwprintf(title, 1200, L"%s（软件 v%s）", g_cfg.title, APP_VERSION_W);
    title[1199] = 0;
    SetWindowTextW(g_hmain, title);
}

/* ---------- 任务执行 ---------- */

typedef struct {
    int model_index;
    int trigger;
    wchar_t drive;
    CFGMODEL model;          /* 启动时按值深拷贝的机型配置（~56KB）：任务线程只读快照，不再引用 g_cfg */
    wchar_t version[64];     /* 版本号快照（日志用，避免并发读到半新半旧的 g_cfg.version） */
} TASKARG;

/* 每一步执行前复核目标盘身份（与任务启动时的基线对比）。
 * 返回 TRUE=一致（或启动时未取到基线，无法判断），FALSE=已变化（任务必须中止）。 */
static BOOL task_verify_target(wchar_t drv, BOOL have_want, const DEVSIG* want)
{
    if (!have_want) return TRUE;
    DEVSIG cur;
    BOOL cap = dev_capture_sig(drv, &cur);
    if (cap && dev_sig_same_device(want, &cur)) return TRUE;

    wchar_t a[320] = L"", b[320] = L"";
    dev_sig_describe(want, a, 320);
    if (cap) dev_sig_describe(&cur, b, 320);
    else     lstrcpynW(b, L"无法读取（盘可能已拔出）", 320);
    applog(CH_ENGINE, LOG_ERR, L"[GUARD] 目标盘已变化（原 %s / 现 %s），任务中止", a, b);
    /* 守卫中止不经过 op_* 的 fail()：显式投递错误徽标，避免界面仍显示“正常”。
     * P2-9：统一用 post_owned_string 投递堆串（失败由 helper 释放，这里只记 WARN）。 */
    if (g_hmain &&
        !post_owned_string(g_hmain, WM_APP_ERROR, 1,
                           L"目标盘已变化，任务中止（详见 CMD-A 日志）"))
        filelog(LOG_WARN, L"[GUARD] 投递错误徽标失败（主窗口不可用）");
    return FALSE;
}

/* 单步结果归类：OP_FAIL→失败标志；OP_STOPPED→停止标志；OP_SKIP_CFG→跳过计数；
 * 其余（OP_OK）→执行计数。任务聚合不再用 rc |=：配置屏蔽不能淹没“程序自行降级/未实现”。 */
static void task_account(int r, int* exec, int* skip, BOOL* fail, BOOL* stop)
{
    if (r == OP_FAIL) *fail = TRUE;
    else if (r == OP_STOPPED) *stop = TRUE;
    else if (r == OP_SKIP_CFG) (*skip)++;
    else (*exec)++;
}

static DWORD WINAPI task_thread(LPVOID p)
{
    TASKARG* a = (TASKARG*)p;
    CFGMODEL* m = &a->model;   /* 只读快照：任务运行中 UI 修改/重载配置不影响本任务 */
    wchar_t drv = a->drive;

    /* 任务级身份绑定：启动时采集基线，后续每一步执行前复核 */
    DEVSIG want;
    BOOL have_want = dev_capture_sig(drv, &want);
    if (have_want) {
        wchar_t ws[320];
        dev_sig_describe(&want, ws, 320);
        applog(CH_ENGINE, LOG_INFO, L"[GUARD] 任务绑定目标盘：%s", ws);
    } else {
        applog(CH_ENGINE, LOG_WARN,
               L"[GUARD] 未能采集目标盘 %c: 身份基线，本次仅按盘符复核", drv);
    }

    applog(CH_ENGINE, LOG_UI, L"=== 任务开始：%s（%s，盘 %c:） v%s ===",
           m->display, a->trigger == TRIGGER_AUTO ? L"自动" : L"手动",
           drv, a->version);

    /* 结果聚合：执行/跳过计数 + fail/stop 标志（不再用 rc |= 合并）。
     * 配置正常屏蔽（OP_SKIP_CFG）单独计数、不改变成功结论；程序自行降级/未实现（OP_FAIL）必须失败。 */
    int exec = 0, skip = 0;
    BOOL fail = FALSE, stop = FALSE, aborted = FALSE;
    BOOL manual = (a->trigger == TRIGGER_MANUAL);

    /* 停止后首步前检查：已停止则不执行任何步骤，最终 OP_STOPPED（绝不报成功） */
    if (CHECK_STOP()) stop = TRUE;

    /* 步骤 1：USB 信息 */
    if (!stop && !aborted) {
        if (CHECK_STOP()) stop = TRUE;
        else if (!task_verify_target(drv, have_want, &want)) { aborted = TRUE; fail = TRUE; }
        else task_account(op_usbinfo(m, drv), &exec, &skip, &fail, &stop);
    }

    /* 步骤 2：复制（手动触发总是执行；自动触发看 AUTO_COPY） */
    if (!stop && !aborted) {
        if (CHECK_STOP()) stop = TRUE;
        else if (!task_verify_target(drv, have_want, &want)) { aborted = TRUE; fail = TRUE; }
        else if (manual || cfg_get_int(m, L"AUTO_COPY", 1)) {
            task_account(op_write(m, drv, have_want ? &want : NULL), &exec, &skip, &fail, &stop);
        } else {
            skip++;   /* 配置裁剪：AUTO_COPY=0 属“按配置正常屏蔽” */
            applog(CH_ENGINE, LOG_INFO,
                   L"[WRITE] 自动模式且 AUTO_COPY=0，按配置跳过复制（可手动点击[复制]）");
        }
    }

    /* 步骤 3：读取 + 判定 */
    if (!stop && !aborted) {
        if (CHECK_STOP()) stop = TRUE;
        else if (!task_verify_target(drv, have_want, &want)) { aborted = TRUE; fail = TRUE; }
        else task_account(op_read(m, drv), &exec, &skip, &fail, &stop);
    }

    /* 步骤 4：读写测试（手动触发总是执行；自动触发看 AUTO_TEST） */
    if (!stop && !aborted) {
        if (CHECK_STOP()) stop = TRUE;
        else if (!task_verify_target(drv, have_want, &want)) { aborted = TRUE; fail = TRUE; }
        else if (manual || cfg_get_int(m, L"AUTO_TEST", 0)) {
            task_account(op_rwtest(m, drv, have_want ? &want : NULL), &exec, &skip, &fail, &stop);
        } else {
            skip++;   /* 配置裁剪：AUTO_TEST=0 属“按配置正常屏蔽” */
            applog(CH_ENGINE, LOG_INFO,
                   L"[RWTEST] 自动模式且 AUTO_TEST=0，按配置跳过自动测试（可手动点击[读写测试]）");
        }
    }

    /* 最终结果：fail→OP_FAIL；否则 stop→OP_STOPPED；否则全部按配置跳过（exec==0&&skip>0）→OP_SKIP_CFG；否则成功 */
    int final_rc;
    if (fail) final_rc = OP_FAIL;
    else if (stop) final_rc = OP_STOPPED;
    else if (exec == 0 && skip > 0) final_rc = OP_SKIP_CFG;
    else final_rc = OP_OK;

    LOGLEVEL endlv = final_rc == OP_OK ? LOG_OK :
                     final_rc == OP_SKIP_CFG ? LOG_INFO :
                     final_rc == OP_STOPPED ? LOG_WARN : LOG_ERR;
    const wchar_t* endtxt = final_rc == OP_OK ? L"成功" :
                            final_rc == OP_SKIP_CFG ? L"按配置跳过（未执行）" :
                            final_rc == OP_STOPPED ? L"已停止" : L"有错误";
    applog(CH_ENGINE, endlv, L"=== 任务结束（%s）：执行 %d 步，按配置跳过 %d 步 ===",
           endtxt, exec, skip);
    PostMessageW(g_hmain, WM_APP_TASKDONE, (WPARAM)final_rc, 0);
    free(a);
    return 0;
}

void task_run_async(int model_index, int trigger, wchar_t drive)
{
    if (g_closing) return;
    if (g_running) {
        applog(CH_ENGINE, LOG_WARN, L"任务执行中，忽略新请求");
        return;
    }
    if (model_index < 0 || model_index >= g_cfg.nmodels) {
        applog(CH_ENGINE, LOG_ERR, L"任务启动失败：机型索引 %d 越界（当前共 %d 个机型）",
               model_index, g_cfg.nmodels);
        if (!post_owned_string(g_hmain, WM_APP_ERROR, 1,
                               L"任务启动失败：机型索引无效"))
            filelog(LOG_WARN, L"[UI] 投递任务启动失败提示失败");
        return;
    }
    if (!drive) {
        /* 旧逻辑：取第一个可移动盘符 */
        DWORD mask = 0;
        dev_enum_removable(&mask, NULL);
        for (wchar_t d = L'C'; d <= L'Z'; d++)
            if (mask & (1u << (d - L'A'))) { drive = d; break; }
    }
    if (!drive) {
        applog(CH_ENGINE, LOG_ERR, L"没有可移动磁盘");
        if (!post_owned_string(g_hmain, WM_APP_ERROR, 1, L"没有可移动磁盘"))
            filelog(LOG_WARN, L"[UI] 投递“没有可移动磁盘”提示失败");
        return;
    }
    TASKARG* a = (TASKARG*)malloc(sizeof(TASKARG));
    if (!a) {
        applog(CH_ENGINE, LOG_ERR, L"内存不足，无法启动任务");
        return;
    }
    a->model_index = model_index;
    a->trigger = trigger;
    a->drive = drive;
    /* P1-3：按值深拷贝机型配置 + 版本号快照（UI 线程内完成拷贝，worker 只读，不再持有 g_cfg 裸指针） */
    a->model = g_cfg.models[model_index];
    lstrcpynW(a->version, g_cfg.version, 64);
    a->version[63] = 0;
    /* P2-1/P2-2/P2-3：整任务开始——清旧判定/内容、进度归零、错误徽标熄灭，监控状态行显示执行中 */
    g_single_op = -1;
    reset_task_ui(FALSE);
    set_monstatus_task_running();
    ops_reset_stop();                       /* 任务开始前清零停止标志（含复制取消标志） */
    InterlockedExchange(&g_running, 1);
    refresh_buttons();                      /* 立即刷新：[停止]可用、其它按钮禁用 */
    refresh_run_state();   /* 运行模式由监控开关/AUTORUN 决定，不由本次触发方式决定 */
    g_worker = CreateThread(NULL, 0, task_thread, a, 0, NULL);
    if (!g_worker) {
        InterlockedExchange(&g_running, 0);
        ops_reset_stop();
        free(a);
        refresh_buttons();
        applog(CH_ENGINE, LOG_ERR, L"创建任务线程失败");
    }
}

/* ---------- 单操作线程 ---------- */

enum { OP_FORMAT = 0, OP_LABELREAD = 1, OP_COPY = 2, OP_IDENT = 3,
       OP_READ = 4, OP_RWTEST = 5 };

/* P2-3：单操作显示名（用于“复制完成/格式化完成/…”与停止文案） */
static const wchar_t* op_display_name(int op)
{
    switch (op) {
    case OP_COPY:      return L"复制";
    case OP_READ:      return L"读取判定";
    case OP_FORMAT:    return L"格式化";
    case OP_LABELREAD: return L"读取标签";
    case OP_RWTEST:    return L"读写测试";
    case OP_IDENT:     return L"标识生成";
    default:           return L"操作";
    }
}

typedef struct {
    int op;
    int model_index;
    wchar_t drive;
    CFGMODEL model;          /* 启动时按值深拷贝的机型配置：操作线程只读快照，不再引用 g_cfg */
    wchar_t version[64];     /* 版本号快照（日志用） */
} OPARG;

static DWORD WINAPI op_thread(LPVOID p)
{
    OPARG* a = (OPARG*)p;
    CFGMODEL* m = &a->model;   /* 只读快照：操作运行中 UI 修改/重载配置不影响本次操作 */
    int rc = OP_OK;
    /* 停止后首步前检查：已停止则不执行任何操作，TASKDONE wp=OP_STOPPED */
    if (CHECK_STOP()) {
        applog(CH_ENGINE, LOG_WARN, L"[UI] 已请求停止，本次操作取消");
        rc = OP_STOPPED;
    } else {
        /* 单操作也做设备身份绑定：进入前采集基线，作为 expect 传给破坏性/读写操作，
         * 由其做进入/关键点复核（格式化还会在确认后再复核一次） */
        DEVSIG want;
        BOOL have_want = dev_capture_sig(a->drive, &want);
        if (have_want) {
            wchar_t ws[320];
            dev_sig_describe(&want, ws, 320);
            applog(CH_ENGINE, LOG_INFO, L"[GUARD] 单操作绑定目标盘：%s", ws);
        }
        switch (a->op) {
        case OP_FORMAT:     rc = op_format(m, a->drive, g_hmain, have_want ? &want : NULL); break;
        case OP_LABELREAD:  rc = op_label_read(m, a->drive); break;
        case OP_COPY:
            rc = op_write(m, a->drive, have_want ? &want : NULL);
            /* 复制结果徽标：仅 OP_OK/OP_FAIL 更新；按配置跳过(OP_SKIP_CFG)/已停止(OP_STOPPED)不覆盖。
             * P2-9：堆串投递统一走 post_owned_string。 */
            if (rc == OP_OK || rc == OP_FAIL) {
                if (!post_owned_string(g_hmain, WM_APP_VERDICT,
                                       (WPARAM)(rc == OP_OK ? 1 : 2),
                                       rc == OP_OK ? L"Copy Complete!" : L"Copy Failure!"))
                    filelog(LOG_WARN, L"[UI] 投递复制结果徽标失败");
            }
            break;
        case OP_IDENT:      g_ident_index++; rc = op_gen_ident(m, a->drive, g_ident_index); break;
        case OP_READ:       rc = op_read(m, a->drive); break;      /* 判定徽标由 op_read 内部设置 */
        case OP_RWTEST:     rc = op_rwtest(m, a->drive, have_want ? &want : NULL); break;
        }
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
    if (g_closing) return;
    if (g_running) { applog(CH_ENGINE, LOG_WARN, L"任务执行中，请等待完成或停止"); return; }
    if (g_cur_model < 0 || g_cur_model >= g_cfg.nmodels) {
        applog(CH_ENGINE, LOG_ERR, L"操作启动失败：机型索引 %d 越界（当前共 %d 个机型）",
               g_cur_model, g_cfg.nmodels);
        if (!post_owned_string(g_hmain, WM_APP_ERROR, 1,
                               L"操作启动失败：机型索引无效"))
            filelog(LOG_WARN, L"[UI] 投递操作启动失败提示失败");
        return;
    }
    wchar_t drive = get_first_drive();
    if (!drive) {
        applog(CH_ENGINE, LOG_ERR, L"没有可移动磁盘");
        if (!post_owned_string(g_hmain, WM_APP_ERROR, 1, L"没有可移动磁盘"))
            filelog(LOG_WARN, L"[UI] 投递“没有可移动磁盘”提示失败");
        return;
    }
    OPARG* a = (OPARG*)malloc(sizeof(OPARG));
    if (!a) {
        applog(CH_ENGINE, LOG_ERR, L"内存不足，无法启动操作");
        return;
    }
    a->op = op;
    a->model_index = g_cur_model;
    a->drive = drive;
    /* P1-3：按值深拷贝机型配置 + 版本号快照（UI 线程内完成拷贝，worker 只读） */
    a->model = g_cfg.models[g_cur_model];
    lstrcpynW(a->version, g_cfg.version, 64);
    a->version[63] = 0;
    /* P2-1/P2-2/P2-3：单操作开始——记录操作名（完成文案用）、复位旧结果/进度、显示执行中 */
    g_single_op = op;
    reset_task_ui(FALSE);
    set_monstatus_task_running();
    ops_reset_stop();                       /* 任务开始前清零停止标志（含复制取消标志） */
    InterlockedExchange(&g_running, 1);
    refresh_buttons();                      /* 立即刷新：[停止]可用、其它按钮禁用 */
    refresh_run_state();   /* 单操作不改变运行模式：监控开着仍显示 AUTO */
    g_worker = CreateThread(NULL, 0, op_thread, a, 0, NULL);
    if (!g_worker) {
        InterlockedExchange(&g_running, 0);
        ops_reset_stop();
        g_single_op = -1;   /* 清理单操作标记（无 TASKDONE 可等） */
        free(a);
        refresh_buttons();
        applog(CH_ENGINE, LOG_ERR, L"创建操作线程失败");
    }
}

/* 退出时等待工作线程结束：MsgWaitForMultipleObjects + 消息泵（保持界面响应），
 * 返回 TRUE=工作线程已结束（或本来就没有线程）；FALSE=超时。 */
static BOOL wait_worker_exit(DWORD timeout_ms)
{
    DWORD start = GetTickCount();
    for (;;) {
        if (!g_worker) return TRUE;
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeout_ms) return FALSE;
        DWORD r = MsgWaitForMultipleObjects(1, &g_worker, FALSE,
                                            timeout_ms - elapsed, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) return TRUE;
        if (r == WAIT_OBJECT_0 + 1) {
            /* 等待期间继续泵消息：让 WM_APP_TASKDONE 完成 join/清理 */
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    PostQuitMessage((int)msg.wParam);
                    continue;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            /* WAIT_TIMEOUT / WAIT_FAILED */
            return FALSE;
        }
    }
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

    /* PANE_TOP 第二行：读取标签 + 置顶 + AUTO_COPY/AUTO_TEST 只读状态框 + 快捷键
     * （F2：两状态框各 150 缩放宽；快捷键占剩余空间，最小约 220 缩放） */
    int y2 = y + rowh + dpi_scale(4);
    MoveWindow(GetDlgItem(g_hmain, IDC_BTN_READLABEL), m, y2, opw, rowh, TRUE);
    int chkx = m + opw + dpi_scale(12);
    MoveWindow(g_hchk_top, chkx, y2, dpi_scale(80), rowh, TRUE);
    chkx += dpi_scale(80) + dpi_scale(8);
    MoveWindow(g_hchk_autocopy, chkx, y2, dpi_scale(150), rowh, TRUE);
    chkx += dpi_scale(150) + dpi_scale(8);
    MoveWindow(g_hchk_autotest, chkx, y2, dpi_scale(150), rowh, TRUE);
    chkx += dpi_scale(150) + dpi_scale(8);
    int hotw = W - m - chkx;
    if (hotw < dpi_scale(220)) hotw = dpi_scale(220);
    MoveWindow(g_hchk_hotkey, chkx, y2, hotw, rowh, TRUE);

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
    y += rowh + dpi_scale(2);
    /* F3：READRULE 判定规则标签栏：状态行后整宽一行；其余行随之下移 */
    MoveWindow(g_hreadrule, m, y, W - 2 * m, rowh, TRUE);
    y += rowh + dpi_scale(10);

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
    /* PANE_FILE：文件内容左右双窗格（与 PANE_CMDS 同布局），左=第一命中，右=第二命中
     * 命中文件提示行也分左右：1103 为左窗格专属，1108 为右窗格专属 */
    int fhalf = (W - 2 * m - dpi_scale(6)) / 2;
    MoveWindow(g_hmonfile, m, y, fhalf, rowh, TRUE);
    MoveWindow(g_hmonfile2, m + fhalf + dpi_scale(6), y,
               W - 2 * m - fhalf - dpi_scale(6), rowh, TRUE);
    y += rowh + dpi_scale(2);

    int btnrow = rowh + dpi_scale(8);
    int fileh = H - y - btnrow - m * 2;
    if (fileh < dpi_scale(40)) fileh = dpi_scale(40);
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


/* 从磁盘重载配置并刷新主界面（机型下拉/按钮状态/主标题/配置窗口列表）。
 * CONFIG 约 906KB：必须堆分配，禁止在栈上放 CONFIG（P1-15 栈溢出风险）。 */
BOOL reload_config_and_ui(void)
{
    /* 记录重载前的选择（主窗口当前机型、配置窗口当前机型），重载后尽量保持 */
    wchar_t cur_internal[CFG_KEY_LEN] = L"";
    if (g_cur_model >= 0 && g_cur_model < g_cfg.nmodels)
        lstrcpynW(cur_internal, g_cfg.models[g_cur_model].internal, CFG_KEY_LEN);
    wchar_t cfg_keep[CFG_KEY_LEN] = L"";
    if (g_hcfglist && IsWindow(g_hcfglist)) {
        int sel = (int)SendMessageW(g_hcfglist, LB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < g_cfg.nmodels)
            lstrcpynW(cfg_keep, g_cfg.models[sel].internal, CFG_KEY_LEN);
    }

    CONFIG* newcfg = (CONFIG*)malloc(sizeof(CONFIG));
    if (!newcfg) {
        applog(CH_ENGINE, LOG_ERR, L"[CFG] 重载配置失败：内存不足（需 %u 字节）",
               (unsigned)sizeof(CONFIG));
        return FALSE;
    }
    if (!cfg_load(g_ini_path, newcfg)) {
        applog(CH_ENGINE, LOG_ERR, L"[CFG] 重载配置失败：ini 无法解析（%s）", g_ini_path);
        free(newcfg);
        return FALSE;
    }
    g_cfg = *newcfg;
    free(newcfg);

    int newsel = 0;
    SendMessageW(g_hmodel, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_cfg.nmodels; i++) {
        SendMessageW(g_hmodel, CB_ADDSTRING, 0, (LPARAM)g_cfg.models[i].display);
        if (cur_internal[0] && _wcsicmp(g_cfg.models[i].internal, cur_internal) == 0)
            newsel = i;
    }
    g_cur_model = newsel;
    SendMessageW(g_hmodel, CB_SETCURSEL, newsel, 0);

    update_main_title();               /* L14：TITLE/VERSION 变化后刷新主标题 */
    /* TOPMOST 键可能已随保存/机型切换变化：同步复选框与置顶状态 */
    SendMessageW(g_hchk_top, BM_SETCHECK,
                 g_cfg.topmost ? BST_CHECKED : BST_UNCHECKED, 0);
    if (g_hmain)
        SetWindowPos(g_hmain, g_cfg.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    cfg_rebuild_model_list(cfg_keep);  /* L14：配置窗口标题/机型列表/内容刷新（窗口打开时） */
    refresh_buttons();
    return TRUE;
}


/* P1-17：全局热键 Ctrl+Alt+T=读写测试 / Ctrl+Alt+C=复制（单操作，不是整任务）。
 * 使用 MOD_CONTROL|MOD_ALT 避免劫持裸 T/C；任一注册失败回滚并返回 FALSE（不宣称成功）。 */
static BOOL hotkeys_enable(HWND h, BOOL on)
{
    if (!on) {
        UnregisterHotKey(h, ID_HOTKEY_T);
        UnregisterHotKey(h, ID_HOTKEY_C);
        g_hotkey_enabled = FALSE;
        if (g_hmain)
            applog(CH_ENGINE, LOG_UI, L"[UI] 快捷键已注销（Ctrl+Alt+T/C）");
        return TRUE;
    }
    if (g_hotkey_enabled) return TRUE;

    BOOL ok_t = RegisterHotKey(h, ID_HOTKEY_T, MOD_CONTROL | MOD_ALT, 'T');
    DWORD err_t = ok_t ? 0 : GetLastError();
    BOOL ok_c = RegisterHotKey(h, ID_HOTKEY_C, MOD_CONTROL | MOD_ALT, 'C');
    DWORD err_c = ok_c ? 0 : GetLastError();
    if (!ok_t || !ok_c) {
        /* 部分成功也回滚，避免“勾选了却只注册一半”的误报 */
        if (ok_t) UnregisterHotKey(h, ID_HOTKEY_T);
        if (ok_c) UnregisterHotKey(h, ID_HOTKEY_C);
        g_hotkey_enabled = FALSE;
        filelog(LOG_WARN,
                L"[UI] 注册全局热键失败：Ctrl+Alt+T=%s(错误码 %lu)，Ctrl+Alt+C=%s(错误码 %lu)；快捷键未启用",
                ok_t ? L"成功" : L"失败", (unsigned long)err_t,
                ok_c ? L"成功" : L"失败", (unsigned long)err_c);
        return FALSE;
    }
    g_hotkey_enabled = TRUE;
    applog(CH_ENGINE, LOG_UI, L"[UI] 快捷键已注册：Ctrl+Alt+T=读写测试 / Ctrl+Alt+C=复制");
    return TRUE;
}

/* ============================================================
 * 主窗口过程
 * ============================================================ */

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        g_hmain = h;
        /* P2-7：RichEdit 不可用时以 STATIC 样式创建（避免把 ES_* 位误解释为其它 STATIC 样式） */
        UINT st_ro       = g_riched_ok ? ES_READONLY : SS_LEFT;
        UINT st_ro_ctr   = g_riched_ok ? (ES_READONLY | ES_MULTILINE | ES_CENTER) : SS_CENTER;
        UINT st_ro_multi = g_riched_ok ? (ES_MULTILINE | ES_READONLY | WS_VSCROLL) : SS_LEFT;
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
        /* F2：AUTO_COPY/AUTO_TEST 只读状态框（灰色禁用，仅显示当前机型配置；值由 update_auto_flags_ui 同步） */
        g_hchk_autocopy = MKCTL(L"BUTTON", 0, BS_AUTOCHECKBOX | WS_DISABLED,
                                IDC_CHK_AUTO_COPY, L"自动复制(AUTO_COPY)");
        g_hchk_autotest = MKCTL(L"BUTTON", 0, BS_AUTOCHECKBOX | WS_DISABLED,
                                IDC_CHK_AUTO_TEST, L"自动测试(AUTO_TEST)");
        g_hchk_hotkey = MKCTL(L"BUTTON", 0, BS_AUTOCHECKBOX, IDC_CHK_HOTKEY,
                              L"快捷键使能（Ctrl+Alt+T/C）");
        g_hlabel = MKCTL(L"STATIC", 0, SS_LEFT, IDC_STC_LABEL, L"标签：");
        g_hautomode = MKCTL(g_riched_class, 0, st_ro_ctr, IDC_STC_AUTOMODE, L"MANUAL");
        g_hsrcpath = MKCTL(L"STATIC", 0, SS_LEFT | SS_ENDELLIPSIS, IDC_STC_SRCPATH, L"SRCPATH：");
        g_hcount = MKCTL(L"STATIC", 0, SS_CENTER, IDC_STC_INSERTCOUNT, L"计数:0000");
        /* F3：READRULE 判定规则标签栏（AUTO_TEST=1 时才显示内容） */
        g_hreadrule = MKCTL(L"STATIC", 0, SS_LEFT | SS_ENDELLIPSIS, IDC_STC_READRULE, L"");
        g_hdev = MKCTL(L"STATIC", 0, SS_LEFT, IDC_STC_DEVICE, L"设备状态：检测中…");
        g_herr = MKCTL(g_riched_class, 0, st_ro, IDC_STC_ERROR, L"");
        g_hprog = MKCTL(PROGRESS_CLASSW, 0, PBS_SMOOTH, IDC_PBAR_TASK, L"");
        g_hprogtitle = MKCTL(L"STATIC", 0, SS_LEFT, IDC_STC_PROGTITLE, L"任务进度：");
        g_hprogpct = MKCTL(L"STATIC", 0, SS_RIGHT, IDC_STC_PROGPCT, L"0%");

        /* 底部监控窗格 */
        g_hmonstatus = MKCTL(g_riched_class, 0, st_ro_ctr, IDC_STC_MONSTATUS, L"监控已停止");
        g_hmonfile = MKCTL(L"STATIC", 0, SS_CENTER, IDC_STC_MONFILE, L"");
        g_hmonfile2 = MKCTL(L"STATIC", 0, SS_CENTER, IDC_STC_MONFILE2, L"");
        g_hmonbtn[0] = MKCTL(L"BUTTON", 0, BS_OWNERDRAW, IDC_BTN_MONSTART, L"开始监控");
        g_hmonbtn[1] = MKCTL(L"BUTTON", 0, BS_OWNERDRAW, IDC_BTN_MONSTOP, L"停止监控");
        g_hmonbtn[2] = MKCTL(L"BUTTON", 0, BS_OWNERDRAW, IDC_BTN_EXIT, L"退出");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_CLEARCMD, L"清屏");
        g_hcmd[0] = MKCTL(g_riched_class, WS_EX_CLIENTEDGE, st_ro_multi, IDC_EDIT_ENGINE, L"");
        g_hcmd[1] = MKCTL(g_riched_class, WS_EX_CLIENTEDGE, st_ro_multi, IDC_EDIT_IDENT, L"");
        g_hverdict = MKCTL(g_riched_class, 0, st_ro_ctr, IDC_STC_READVERDICT, L"");
        g_hfile[0] = MKCTL(g_riched_class, WS_EX_CLIENTEDGE, st_ro_multi, IDC_EDIT_FILE, L"");
        g_hfile[1] = MKCTL(g_riched_class, WS_EX_CLIENTEDGE, st_ro_multi, IDC_EDIT_FILE2, L"");
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

    case WM_GETMINMAXINFO: {
        /* P2-6：最小窗口尺寸按当前 DPI 缩放，防止缩到负布局。
         * 宽度需容纳第一行（机型下拉 200 + 7 个 88 宽按钮 + 边距）；高度留出各窗格。 */
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = dpi_scale(880);
        mmi->ptMinTrackSize.y = dpi_scale(600);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        /* P3-1：IDC_STC_MONFILE/IDC_STC_MONFILE2 为 STATIC，EM_SETCHARFORMAT 无效；
         * 在此设置文字颜色并返回窗口背景刷（保留居中/字体由控件样式决定）。 */
        HDC hdc = (HDC)wp;
        HWND hctl = (HWND)lp;
        SetBkMode(hdc, TRANSPARENT);
        if (hctl == g_hmonfile)       SetTextColor(hdc, g_monfile_col);
        else if (hctl == g_hmonfile2) SetTextColor(hdc, g_monfile2_col);
        else                          SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

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
        if (g_closing) return 0;   /* 退出等待中：忽略新的界面命令，防止重入 */
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
                        /* F2/F3：reload 内的 refresh 发生在 g_cur_model 更新之前，
                         * 这里补一次，确保 AUTO_COPY/AUTO_TEST 与 READRULE 立即切到新机型 */
                        update_auto_flags_ui();
                        update_readrule_text();
                        applog(CH_ENGINE, LOG_UI, L"[UI] 机型切换为：%s（内部键 %s，配置已重载，与轮询解耦）",
                               g_cfg.models[g_cur_model].display, g_cfg.models[g_cur_model].internal);
                    } else {
                        /* 重载失败：保持原机型选中，不得假报切换成功（L14） */
                        SendMessageW(g_hmodel, CB_SETCURSEL, g_cur_model, 0);
                        applog(CH_ENGINE, LOG_ERR,
                               L"[UI] 切换机型失败：配置重载失败，保持原机型 %s",
                               (g_cur_model >= 0 && g_cur_model < g_cfg.nmodels)
                                   ? g_cfg.models[g_cur_model].internal : L"?");
                    }
                    refresh_buttons();
                }
            }
            break;
        case IDC_BTN_START:
            task_run_async(g_cur_model, TRIGGER_MANUAL, 0);
            break;
        case IDC_BTN_STOP:
            ops_request_stop();
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
                if (g_running) ops_request_stop();
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
                if (!hotkeys_enable(h, on)) {
                    /* 注册失败：恢复未勾选，不得显示为“已注册” */
                    SendMessageW(g_hchk_hotkey, BM_SETCHECK, BST_UNCHECKED, 0);
                    MessageBoxW(h,
                        L"注册全局热键 Ctrl+Alt+T / Ctrl+Alt+C 失败（可能被其它程序占用）。\n"
                        L"请关闭占用程序后重试。",
                        L"快捷键", MB_OK | MB_ICONWARNING);
                }
            }
            break;
        }
        return 0;
    }

    case WM_HOTKEY:
        if (g_closing) return 0;
        /* P1-17：热键=单操作（Ctrl+Alt+T 读写测试 / Ctrl+Alt+C 复制），任务执行中忽略并记日志 */
        if (wp == ID_HOTKEY_T) {
            applog(CH_ENGINE, LOG_UI, L"[UI] 热键 Ctrl+Alt+T：读写测试");
            if (g_running) {
                applog(CH_ENGINE, LOG_WARN, L"[UI] 任务执行中，忽略热键（读写测试）");
                return 0;
            }
            run_single(OP_RWTEST);
        } else if (wp == ID_HOTKEY_C) {
            applog(CH_ENGINE, LOG_UI, L"[UI] 热键 Ctrl+Alt+C：复制");
            if (g_running) {
                applog(CH_ENGINE, LOG_WARN, L"[UI] 任务执行中，忽略热键（复制）");
                return 0;
            }
            run_single(OP_COPY);
        }
        return 0;

    case WM_APP_LOG: {
        int ch = (int)(wp & 0xFFFF);
        int lv = (int)(wp >> 16);
        con_append(g_hcmd[(ch >= 0 && ch < 2) ? ch : 0], (wchar_t*)lp, (LOGLEVEL)lv);
        free((void*)lp);
        return 0;
    }
    case WM_APP_PROGRESS:
        /* P2-2：ops 侧字节/文件/格式化进度统一换算 0..100 后投递，这里只走唯一更新入口 */
        post_progress_pct((int)wp);
        return 0;
    case WM_APP_DEVCOUNT: {
        /* 新协议：wparam = 本次变化的盘符位图（可多位），lparam = 1插入 / 0拔出。
         * 同一次轮询最多两条消息（先拔出位图、后插入位图），不再传最终全量 mask。 */
        DWORD bits = (DWORD)wp;
        BOOL inserted = lp != 0;
        wchar_t first = 0;
        /* 1) 先把本消息所有位增量维护进 g_dev_mask（不求差） */
        for (wchar_t d = L'C'; d <= L'Z'; d++) {
            DWORD bit = 1u << (d - L'A');
            if (!(bits & bit)) continue;
            if (inserted) {
                g_dev_mask |= bit;
                if (!first) first = d;
            } else {
                g_dev_mask &= ~bit;
            }
        }
        /* 2) 所有位更新完统一刷新一次设备/标签行 */
        update_device_text();

        if (inserted) {
            /* 3) 新盘会话清理：只在“有插入且当前无任务运行”时做一次（不在位循环里重复） */
            if (!g_running) {
                SetWindowTextW(g_hcmd[0], L"");   /* 保留 batch2：新盘会话清 CMD-A */
                reset_task_ui(TRUE);              /* P2-1：判定/内容/命中行/进度/徽标统一复位 */
            }
            for (wchar_t d = L'C'; d <= L'Z'; d++) {
                DWORD bit = 1u << (d - L'A');
                if (!(bits & bit)) continue;
                applog(CH_ENGINE, LOG_UI, L"[DEV] 检测到可移动磁盘 %c:\\ 插入%s", d,
                       g_running ? L"（任务执行中：只记日志，不清屏）" : L"");
                /* 插拔计数器：仅监控开启时计数；4 位循环（9999→0000），退出程序不保存 */
                if (g_monitoring) {
                    InterlockedIncrement(&g_inscount);
                    update_count_text();
                }
            }
            /* 4) 自动触发 / 待处理：多个插入只取第一个盘符 */
            if (first && run_auto_mode()) {
                if (!g_running) {
                    task_run_async(g_cur_model, TRIGGER_AUTO, first);   /* 用刚插入的盘符 */
                } else if (!g_pending_drive) {
                    g_pending_drive = first;
                    applog(CH_ENGINE, LOG_UI,
                           L"[DEV] 任务执行中插入 %c:\\：记为待处理盘，任务结束后若自动模式仍开启则补执行",
                           first);
                } else {
                    applog(CH_ENGINE, LOG_UI,
                           L"[DEV] 任务执行中插入 %c:\\：已有待处理盘 %c:\\（先到先处理），本次不覆盖",
                           first, g_pending_drive);
                }
            }
        } else {
            for (wchar_t d = L'C'; d <= L'Z'; d++) {
                DWORD bit = 1u << (d - L'A');
                if (!(bits & bit)) continue;
                applog(CH_ENGINE, LOG_UI, L"[DEV] 可移动磁盘 %c:\\ 已拔出", d);
                if (g_pending_drive == d) {
                    g_pending_drive = 0;
                    applog(CH_ENGINE, LOG_UI,
                           L"[DEV] 待处理盘 %c:\\ 已拔出，取消其自动执行", d);
                }
            }
            /* P2-1/P2-2：拔出——进度归零；空闲时同时清判定/内容/命中行，避免残留上一盘结果 */
            post_progress_pct(0);
            if (!g_running) {
                SetWindowTextW(g_hverdict, L"");
                if (g_hfile[0]) SetWindowTextW(g_hfile[0], L"");
                if (g_hfile[1]) SetWindowTextW(g_hfile[1], L"");
                set_static_text(g_hmonfile, L"", RGB(0, 90, 200));
                set_static_text(g_hmonfile2, L"", RGB(0, 90, 200));
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
        /* wparam：0=左窗格（1102+1103）；1=右窗格专属提示（仅 1108） */
        wchar_t* s = (wchar_t*)lp;
        if (s) {
            wchar_t* nl = wcschr(s, L'\n');
            if (wp == 1) {
                const wchar_t* sub = nl ? nl + 1 : s;
                set_static_text(g_hmonfile2, sub, RGB(0, 90, 200));
            }
            else if (nl) { *nl = 0; set_monstatus(s, nl + 1, RGB(60, 60, 60)); }
            else set_monstatus(s, L"", RGB(60, 60, 60));
            free(s);
        }
        return 0;
    }
    case WM_APP_DEVREFRESH:
        /* 格式化等操作完成后设备信息变化：重新采集并刷新设备/标签行 */
        update_device_text();
        return 0;
    case WM_APP_TASKDONE: {
        int rc = (int)wp;
        InterlockedExchange(&g_running, 0);
        /* join 工作线程（消息在线程末尾投递，2s 足够）并释放句柄 */
        if (g_worker) {
            WaitForSingleObject(g_worker, 2000);
            CloseHandle(g_worker);
            g_worker = NULL;
        }
        ops_reset_stop();

        /* P2-3：单操作显示“<操作名>完成/已停止”，整任务仍显示“任务完成/按配置跳过/已停止” */
        const wchar_t* opname = (g_single_op >= 0) ? op_display_name(g_single_op) : NULL;
        wchar_t badge[160];
        if (rc == OP_OK) {
            if (opname) {
                _snwprintf(badge, 160, L"%s完成", opname);
                badge[159] = 0;
                set_ok_badge(badge);
            } else {
                set_ok_badge(L"任务完成");
            }
        } else if (rc == OP_SKIP_CFG) {
            if (opname) {
                _snwprintf(badge, 160, L"%s：按配置跳过（未执行）", opname);
                badge[159] = 0;
                set_ok_badge(badge);
            } else {
                set_ok_badge(L"按配置跳过（未执行）");
            }
        } else if (rc == OP_STOPPED) {
            if (opname) {
                _snwprintf(badge, 160, L"%s已停止", opname);
                badge[159] = 0;
                set_ok_badge(badge);
            } else {
                set_ok_badge(L"任务已停止");
            }
        }
        g_single_op = -1;

        /* P2-1/P2-2：结束后的界面状态——OK 保留结果且进度 100；SKIP/STOPPED 清旧判定与内容，
         * 避免误读上一轮；FAIL 保留失败现场（红徽标由 ops 置位）。 */
        post_progress_pct(rc == OP_OK ? 100 : 0);
        if (rc == OP_SKIP_CFG || rc == OP_STOPPED) {
            SetWindowTextW(g_hverdict, L"");
            SetWindowTextW(g_hfile[0], L"");
            SetWindowTextW(g_hfile[1], L"");
            set_static_text(g_hmonfile, L"", RGB(0, 90, 200));
            set_static_text(g_hmonfile2, L"", RGB(0, 90, 200));
        }
        if (rc == OP_FAIL)
            set_monstatus(L"任务失败（详见 CMD-A 日志）", L"", RGB(200, 0, 0));
        else if (rc == OP_STOPPED)
            set_monstatus(L"任务已停止", L"", RGB(200, 0, 0));
        else
            set_monstatus_idle();
        refresh_buttons();

        /* 待处理盘：任务结束后若仍插入且仍自动模式 → 补触发一次；失败/停止不自动重试，避免失败循环 */
        if (g_pending_drive) {
            wchar_t pd = g_pending_drive;
            g_pending_drive = 0;   /* 触发前先清空（无论后续是否真的触发） */
            DWORD bit = 1u << (pd - L'A');
            if (g_closing) {
                applog(CH_ENGINE, LOG_UI,
                       L"[DEV] 退出流程中，取消待处理盘 %c:\\ 的自动执行", pd);
            } else if (rc == OP_FAIL || rc == OP_STOPPED) {
                applog(CH_ENGINE, LOG_WARN,
                       L"[DEV] 上一任务%s，取消待处理盘 %c:\\ 的自动执行（不自动重试）",
                       rc == OP_FAIL ? L"失败" : L"被停止", pd);
            } else if (!(g_dev_mask & bit)) {
                applog(CH_ENGINE, LOG_UI,
                       L"[DEV] 待处理盘 %c:\\ 已不在位，取消自动执行", pd);
            } else if (!run_auto_mode()) {
                applog(CH_ENGINE, LOG_UI,
                       L"[DEV] 自动模式已关闭，取消待处理盘 %c:\\ 的自动执行", pd);
            } else if (!g_running) {
                applog(CH_ENGINE, LOG_UI,
                       L"[DEV] 任务结束，补执行待处理盘 %c:\\ 的自动任务", pd);
                task_run_async(g_cur_model, TRIGGER_AUTO, pd);
            }
        }
        return 0;
    }

    case WM_CLOSE:
        if (g_closing) return 0;   /* 退出等待中：忽略重复关闭请求 */
        if (g_running) {
            if (MessageBoxW(h, L"任务正在执行，确认停止任务并退出？", L"退出确认",
                            MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
                return 0;
            /* 先请求停止，再带消息泵等待工作线程结束（最长 30s，期间 g_closing 防重入） */
            g_closing = TRUE;
            g_pending_drive = 0;   /* 退出流程：取消待处理盘的自动补执行 */
            ops_request_stop();
            applog(CH_ENGINE, LOG_UI, L"[UI] 退出：已请求停止任务，等待工作线程结束…");
            BOOL forced = FALSE;
            if (!wait_worker_exit(30000)) {
                applog(CH_ENGINE, LOG_WARN,
                       L"[UI] 退出：等待工作线程超时（30s），强制退出（任务可能未完全结束）");
                forced = TRUE;
            }
            /* 等待期间 WM_APP_TASKDONE 可能已完成 join，这里兜底 */
            if (g_worker) {
                if (WaitForSingleObject(g_worker, 2000) != WAIT_OBJECT_0) {
                    applog(CH_ENGINE, LOG_WARN, L"[UI] 退出：工作线程仍未结束，强制退出");
                    forced = TRUE;
                }
                CloseHandle(g_worker);
                g_worker = NULL;
            }
            if (forced) {
                /* P3-6：强制退出 → 进程退出码 3（main/gui_run 返回给调用方） */
                g_exit_code = 3;
                filelog(LOG_ERR, L"[SYS] 强制退出：工作线程未在超时内结束（退出码 3，任务可能未完成）");
            }
            InterlockedExchange(&g_running, 0);
            ops_reset_stop();
            refresh_buttons();
        }
        g_pending_drive = 0;   /* 退出：清空待处理盘 */
        DestroyWindow(h);
        return 0;

    case WM_DESTROY:
        dev_poll_stop();
        ops_request_stop();   /* 兜底：退出后不再启动新操作 */
        g_pending_drive = 0;
        if (g_worker) {
            if (WaitForSingleObject(g_worker, 2000) != WAIT_OBJECT_0) {
                /* 兜底 join 仍失败：按强制退出处理（退出码 3），避免报告正常退出 */
                g_exit_code = 3;
                filelog(LOG_ERR, L"[SYS] 销毁窗口时工作线程仍未结束（退出码 3，任务可能未完成）");
            }
            CloseHandle(g_worker);
            g_worker = NULL;
        }
        PostQuitMessage(g_exit_code);   /* P3-6：正常 0 / 强制退出 3 */
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ---------- GUI 主循环 ---------- */

int gui_run(HINSTANCE hInst, int nShow)
{
    g_hinst = hInst;
    g_exit_code = 0;   /* P3-6：单进程只运行一次 gui_run，显式复位退出码 */
    dpi_init();

    /* P2-7：RichEdit 类回退链 Msftedit(RICHEDIT50W)→Riched20(RICHEDIT20W)→STATIC。
     * 早期版本固定创建 RICHEDIT50W，导致 Riched20 回退无效。 */
    HMODULE me = LoadLibraryW(L"Msftedit.dll");
    if (me) {
        g_riched_class = L"RICHEDIT50W";
        g_riched_ok = TRUE;
    } else {
        me = LoadLibraryW(L"Riched20.dll");
        if (me) {
            g_riched_class = L"RICHEDIT20W";
            g_riched_ok = TRUE;
        } else {
            g_riched_class = L"STATIC";
            g_riched_ok = FALSE;
            filelog(LOG_WARN,
                    L"[UI] RichEdit 不可用（Msftedit.dll/Riched20.dll 均加载失败），"
                    L"文本窗格回退 STATIC 只读显示（禁用富文本格式化）");
        }
    }

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON),
                                 IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);   /* 资源缺失时回退 */
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = UDK_MAIN_WNDCLASS;
    RegisterClassW(&wc);

    cfg_register_class(hInst);   /* 配置窗口类注册（gui_config.c） */

    g_dpi = get_system_dpi();

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int W = dpi_scale(880), H = dpi_scale(760);   /* 纵向加高：文件内容区不滚动可显示 10 行以上 */
    int x = wa.right > W ? (wa.right - W) / 2 : CW_USEDEFAULT;
    int y2 = wa.bottom > H ? (wa.bottom - H) / 3 : CW_USEDEFAULT;

    /* 主标题由 update_main_title() 统一生成（g_cfg.title/version），reload 后复用同一函数 */
    HWND h = CreateWindowExW(0, UDK_MAIN_WNDCLASS, L"",
                             WS_OVERLAPPEDWINDOW,
                             x, y2, W, H, NULL, NULL, hInst, NULL);
    if (!h) return -1;
    update_main_title();

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

    /* TOPMOST：初始置顶状态由配置键 TOPMOST 决定（缺省 1=开启；勾选框同步显示） */
    SendMessageW(g_hchk_top, BM_SETCHECK,
                 g_cfg.topmost ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowPos(h, g_cfg.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE);
    applog(CH_ENGINE, LOG_UI, L"[SYS] 窗口置顶初始状态：%s（TOPMOST=%d）",
           g_cfg.topmost ? L"开启" : L"关闭", g_cfg.topmost);

    /* 快捷键默认不注册，由"快捷键使能"复选框控制（Ctrl+Alt+T=读写测试 / Ctrl+Alt+C=复制） */

    /* B 线（task-22）要求：启动日志同时显示配置版本与软件版本，便于现场区分两套版本 */
    if (g_cfg.version[0])
        applog(CH_ENGINE, LOG_UI, L"[SYS] %s v%s（软件 v%s）启动（ini：%s）",
               g_cfg.title, g_cfg.version, APP_VERSION_W, g_ini_path);
    else
        applog(CH_ENGINE, LOG_UI, L"[SYS] %s（软件 v%s）启动（ini：%s）",
               g_cfg.title, APP_VERSION_W, g_ini_path);
    applog(CH_ENGINE, LOG_UI, L"[SYS] 已加载 %d 个机型，当前默认：%s",
           g_cfg.nmodels, g_cfg.models[g_cur_model].internal);
    if (!g_cfg.adminpass[0])
        applog(CH_ENGINE, LOG_WARN, L"[SYS] 未设置管理员密码，配置窗口仅只读");

    /* 启动设备检测：先枚举一次作为基线（已插着的盘不计数、不触发任务），再启动轮询。
     * 顺序很重要：先有 g_dev_mask 基线，轮询首轮才不会把“已存在的盘”误报为插入事件。 */
    dev_enum_removable(&g_dev_mask, NULL);
    update_device_text();
    dev_poll_start(h);

    /* 退出码（P3-6）：0=正常关闭；1=GetMessageW 失败；3=退出等待工作线程超时强制退出 */
    MSG msg;
    int ret;
    while ((ret = GetMessageW(&msg, NULL, 0, 0)) != 0) {
        if (ret == -1) {
            DWORD err = GetLastError();
            filelog(LOG_ERR, L"[SYS] 消息循环失败：GetMessageW 返回 -1（错误码 %lu），退出",
                    (unsigned long)err);
            g_exit_code = 1;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterHotKey(h, ID_HOTKEY_T);
    UnregisterHotKey(h, ID_HOTKEY_C);
    if (me) FreeLibrary(me);
    return g_exit_code;
}
