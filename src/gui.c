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

/* ---------- 全局 ---------- */
CONFIG g_cfg;
wchar_t g_ini_path[MAX_PATH] = L"";
volatile LONG g_running = 0;

static HINSTANCE g_hinst = NULL;
static HWND g_hmain = NULL;
static HWND g_hmodel = NULL, g_hmode = NULL, g_hlabel = NULL, g_hdev = NULL,
            g_herr = NULL, g_hprog = NULL, g_hcmd[2] = { NULL, NULL },
            g_hverdict = NULL, g_hfile = NULL, g_hchk_top = NULL;
static HFONT g_font = NULL, g_font_mono = NULL;
static UINT g_dpi = 96;
static DWORD g_dev_mask = 0;
static int g_cur_model = 0;         /* 当前机型索引（与轮询解耦） */
static int g_ident_index = 0;       /* 标识生成计数 */

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
    int h1 = -MulDiv(9, (int)g_dpi, 72);
    g_font = CreateFontW(h1, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_mono = CreateFontW(h1, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    if (g_hmain) {
        EnumChildWindows(g_hmain, font_enum, (LPARAM)g_font);
        if (g_hcmd[0]) SendMessageW(g_hcmd[0], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hcmd[1]) SendMessageW(g_hcmd[1], WM_SETFONT, (WPARAM)g_font_mono, TRUE);
        if (g_hfile)   SendMessageW(g_hfile, WM_SETFONT, (WPARAM)g_font_mono, TRUE);
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
        { IDC_BTN_FORMAT,     L"BTN_FORMAT",     L"格式化",   L"ENABLE_FORMAT" },
        { IDC_BTN_READLABEL,  L"BTN_READLABEL",  L"读取标签", NULL },
        { IDC_BTN_WRITELABEL, L"BTN_WRITELABEL", L"写入标签", L"ENABLE_WRITE" },
        { IDC_BTN_CONFIG,     NULL,              L"配置",     NULL },
    };
    for (int i = 0; i < 6; i++) {
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
}

static void set_mode_badge(int trigger)
{
    HWND h = g_hmode;
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD;
    cf.dwEffects = CFE_BOLD;
    if (trigger == TRIGGER_AUTO) {
        SetWindowTextW(h, L"● 自动模式");
        cf.crTextColor = RGB(0, 130, 0);
    } else {
        SetWindowTextW(h, L"● 手动模式");
        cf.crTextColor = RGB(0, 90, 200);
    }
    SendMessageW(h, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
}

static void set_error_badge(BOOL on, const wchar_t* msg)
{
    HWND h = g_herr;
    if (on) {
        wchar_t txt[300];
        _snwprintf(txt, 300, L"✕ 出错%s%s", msg && msg[0] ? L"：" : L"", msg ? msg : L"");
        SetWindowTextW(h, txt);
        CHARFORMAT2W cf; memset(&cf, 0, sizeof(cf));
        cf.cbSize = sizeof(cf); cf.dwMask = CFM_COLOR | CFM_BOLD;
        cf.dwEffects = CFE_BOLD; cf.crTextColor = RGB(200, 0, 0);
        SendMessageW(h, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
        wchar_t t[256];
        GetWindowTextW(g_hmain, t, 200);
        if (!wcsstr(t, L"[!]")) {
            wchar_t t2[260];
            _snwprintf(t2, 260, L"%s [!]", t);
            SetWindowTextW(g_hmain, t2);
        }
    } else {
        SetWindowTextW(h, L"");
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

static void update_device_text(void)
{
    if (!g_dev_mask) {
        SetWindowTextW(g_hdev, L"设备状态：未检测到可移动磁盘（等待插入…）");
        SetWindowTextW(g_hlabel, L"标签：");
        return;
    }
    wchar_t txt[512] = L"设备状态：";
    for (wchar_t d = L'C'; d <= L'Z'; d++) {
        if (g_dev_mask & (1u << (d - L'A'))) {
            DEVINFO di;
            dev_get_info(d, &di);
            wchar_t one[160];
            _snwprintf(one, 160, L"%c: [%s|%.1fGB]  ", d,
                       di.volume[0] ? di.volume : L"无卷标",
                       di.total_bytes / 1073741824.0);
            lstrcatW(txt, one);
        }
    }
    SetWindowTextW(g_hdev, txt);
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

    PostMessageW(g_hmain, WM_APP_MODE, a->trigger, 0);
    applog(CH_ENGINE, LOG_UI, L"=== 任务开始：%s（%s，盘 %c:） v%s ===",
           m->display, a->trigger == TRIGGER_AUTO ? L"自动" : L"手动",
           drv, g_cfg.version);

    int rc = 0;
    rc |= op_usbinfo(m, drv);
    if (!CHECK_STOP()) rc |= op_write(m, drv);
    if (!CHECK_STOP()) rc |= op_read(m, drv);
    if (!CHECK_STOP()) rc |= op_rwtest(m, drv);

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
    HANDLE h = CreateThread(NULL, 0, task_thread, a, 0, NULL);
    if (h) CloseHandle(h);
    else { InterlockedExchange(&g_running, 0); free(a); }
}

/* ---------- 单操作线程 ---------- */

enum { OP_FORMAT = 0, OP_LABELREAD = 1, OP_LABELWRITE = 2, OP_IDENT = 3 };

typedef struct {
    int op;
    int model_index;
    wchar_t drive;
} OPARG;

static DWORD WINAPI op_thread(LPVOID p)
{
    OPARG* a = (OPARG*)p;
    CFGMODEL* m = &g_cfg.models[a->model_index];
    switch (a->op) {
    case OP_FORMAT:     op_format(m, a->drive, g_hmain); break;
    case OP_LABELREAD:  op_label_read(m, a->drive); break;
    case OP_LABELWRITE: op_label_write(m, a->drive); break;
    case OP_IDENT:      g_ident_index++; op_gen_ident(m, a->drive, g_ident_index); break;
    }
    PostMessageW(g_hmain, WM_APP_TASKDONE, 0, 0);
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

    /* PANE_TOP：机型下拉 + 按钮行 */
    MoveWindow(g_hmodel, m, y, dpi_scale(220), rowh, TRUE);
    int x = m + dpi_scale(226);
    struct { int id; int w; } btns[] = {
        { IDC_BTN_START, 70 }, { IDC_BTN_STOP, 60 }, { IDC_BTN_FORMAT, 70 },
        { IDC_BTN_READLABEL, 80 }, { IDC_BTN_WRITELABEL, 80 },
    };
    for (int i = 0; i < 5; i++) {
        HWND hb = GetDlgItem(g_hmain, btns[i].id);
        int bw = dpi_scale(btns[i].w);
        MoveWindow(hb, x, y, bw, rowh, TRUE);
        x += bw + dpi_scale(4);
    }
    MoveWindow(GetDlgItem(g_hmain, IDC_BTN_CONFIG), x, y, dpi_scale(60), rowh, TRUE);
    x += dpi_scale(66);
    MoveWindow(g_hchk_top, x, y, dpi_scale(86), rowh, TRUE);
    x += dpi_scale(90);
    MoveWindow(g_hmode, x, y, dpi_scale(110), rowh, TRUE);

    y += rowh + dpi_scale(4);
    /* PANE_LABEL / PANE_STATUS（设备状态 + 出错徽标） */
    MoveWindow(g_hlabel, m, y, W - 2 * m, rowh, TRUE);
    y += rowh + dpi_scale(2);
    int devw = (W - 2 * m) * 2 / 3;
    MoveWindow(g_hdev, m, y, devw, rowh, TRUE);
    MoveWindow(g_herr, m + devw + dpi_scale(6), y,
               W - 2 * m - devw - dpi_scale(6), rowh, TRUE);
    y += rowh + dpi_scale(4);
    /* PANE_TASK 进度条 */
    MoveWindow(g_hprog, m, y + dpi_scale(5), W - 2 * m, dpi_scale(16), TRUE);
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

    /* PANE_FILE：判定徽标 + 内容 */
    MoveWindow(g_hverdict, m, y, dpi_scale(70), rowh, TRUE);
    int fileh = H - y - m;
    if (fileh < dpi_scale(40)) fileh = dpi_scale(40);
    MoveWindow(g_hfile, m + dpi_scale(78), y, W - 2 * m - dpi_scale(78), fileh - dpi_scale(2), TRUE);
}

/* ============================================================
 * 配置窗口
 * ============================================================ */

static BOOL g_cfg_unlocked = FALSE;
static HWND g_hcfg = NULL, g_hcfglist = NULL, g_hcfgtext = NULL, g_hcfgpass = NULL,
            g_hcfgstate = NULL, g_hcfgkey = NULL, g_hcfgval = NULL;
static int g_cfg_failcount = 0;

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
}

static void cfg_apply_lock(void)
{
    EnableWindow(g_hcfgkey, g_cfg_unlocked);
    EnableWindow(g_hcfgval, g_cfg_unlocked);
    SetWindowTextW(g_hcfgstate, g_cfg_unlocked ? L"状态：已解锁（可编辑保存）" : L"状态：只读（未授权）");
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
    #define CFGBTN(id, txt, x2, y2, w2, h2) do { \
        HWND hc = CreateWindowExW(0, L"BUTTON", txt, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, \
                                  x2, y2, w2, h2, h, (HMENU)(INT_PTR)(id), g_hinst, NULL); \
        SendMessageW(hc, WM_SETFONT, (WPARAM)f, TRUE); } while (0)

    CFGSTC(0, L"机型：", m, y + dpi_scale(4), dpi_scale(50), rowh);
    CFGLIST(IDC_CFG_LIST_MODELS, m + dpi_scale(50), y, dpi_scale(240), rowh * 4,
            LBS_NOTIFY | LBS_NOINTEGRALHEIGHT);
    y += rowh * 4 + dpi_scale(8);

    CFGSTC(0, L"键（如 VOLUME）：", m, y + dpi_scale(4), dpi_scale(160), rowh);
    CFGEDIT(IDC_EDIT_CFGKEY, m + dpi_scale(165), y, dpi_scale(320), rowh, ES_AUTOHSCROLL);
    y += rowh + dpi_scale(4);
    CFGSTC(0, L"新值：", m, y + dpi_scale(4), dpi_scale(160), rowh);
    CFGEDIT(IDC_EDIT_CFGVAL, m + dpi_scale(165), y, dpi_scale(320), rowh, ES_AUTOHSCROLL);
    y += rowh + dpi_scale(4);

    CFGBTN(IDC_BTN_UNLOCK, L"解锁编辑", m, y, dpi_scale(90), rowh);
    CFGSTC(0, L"密码：", m + dpi_scale(100), y + dpi_scale(4), dpi_scale(60), rowh);
    CFGEDIT(IDC_CFG_EDIT_PASS, m + dpi_scale(160), y, dpi_scale(140), rowh,
            ES_PASSWORD | ES_AUTOHSCROLL);
    CFGBTN(IDC_BTN_SAVECFG, L"保存修改", m + dpi_scale(320), y, dpi_scale(90), rowh);
    CFGBTN(IDC_BTN_CLOSECFG, L"关闭", m + dpi_scale(418), y, dpi_scale(60), rowh);
    y += rowh + dpi_scale(8);
    CFGSTC(IDC_STC_CFGSTATE, L"状态：只读（未授权）", m, y, dpi_scale(320), rowh);
    y += rowh + dpi_scale(8);

    CFGSTC(0, L"配置内容：", m, y, dpi_scale(120), rowh);
    y += rowh;
    CFGEDIT(IDC_EDIT_CFGTEXT, m, y, dpi_scale(500), dpi_scale(260),
            ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY);
    #undef CFGSTC
    #undef CFGLIST
    #undef CFGEDIT
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
        g_hcfgkey = GetDlgItem(h, IDC_EDIT_CFGKEY);
        g_hcfgval = GetDlgItem(h, IDC_EDIT_CFGVAL);
        for (int i = 0; i < g_cfg.nmodels; i++)
            SendMessageW(g_hcfglist, LB_ADDSTRING, 0, (LPARAM)g_cfg.models[i].display);
        SendMessageW(g_hcfglist, LB_SETCURSEL, 0, 0);
        cfg_show_model();
        cfg_apply_lock();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CFG_LIST_MODELS:
            if (HIWORD(wp) == LBN_SELCHANGE) cfg_show_model();
            break;
        case IDC_BTN_UNLOCK: {
            wchar_t pass[CFG_VAL_LEN];
            GetWindowTextW(g_hcfgpass, pass, CFG_VAL_LEN);
            if (!g_cfg.adminpass[0]) {
                MessageBoxW(h, L"ini 未设置管理员密码（ADMINPASS），暂不支持解锁编辑。",
                            L"提示", MB_OK | MB_ICONINFORMATION);
            } else if (wcscmp(pass, g_cfg.adminpass) == 0) {
                g_cfg_unlocked = TRUE;
                g_cfg_failcount = 0;
                cfg_apply_lock();
                applog(CH_ENGINE, LOG_UI, L"[CFG] 配置窗口已解锁编辑");
            } else {
                g_cfg_failcount++;
                wchar_t msgb[128];
                _snwprintf(msgb, 128, L"密码错误（第 %d 次，连续 3 次锁定）", g_cfg_failcount);
                MessageBoxW(h, msgb, L"解锁失败", MB_OK | MB_ICONWARNING);
                if (g_cfg_failcount >= 3) {
                    EnableWindow(g_hcfgpass, FALSE);
                    EnableWindow(GetDlgItem(h, IDC_BTN_UNLOCK), FALSE);
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
            wchar_t key[CFG_KEY_LEN], val[CFG_VAL_LEN];
            GetWindowTextW(g_hcfgkey, key, CFG_KEY_LEN);
            GetWindowTextW(g_hcfgval, val, CFG_VAL_LEN);
            if (!key[0]) { MessageBoxW(h, L"请输入要修改的键名。", L"提示", MB_OK); break; }
            int sel = (int)SendMessageW(g_hcfglist, LB_GETCURSEL, 0, 0);
            if (sel < 0) break;
            if (cfg_save_key(g_ini_path, g_cfg.models[sel].internal, key, val)) {
                int oldsel = g_cur_model;
                cfg_load(g_ini_path, &g_cfg);
                /* 刷新主窗口机型下拉 */
                SendMessageW(g_hmodel, CB_RESETCONTENT, 0, 0);
                for (int i = 0; i < g_cfg.nmodels; i++)
                    SendMessageW(g_hmodel, CB_ADDSTRING, 0, (LPARAM)g_cfg.models[i].display);
                g_cur_model = oldsel < g_cfg.nmodels ? oldsel : 0;
                SendMessageW(g_hmodel, CB_SETCURSEL, g_cur_model, 0);
                refresh_buttons();
                cfg_show_model();
                applog(CH_ENGINE, LOG_OK, L"[CFG] 已保存 %s.%s=%s",
                       g_cfg.models[sel].internal, key, val);
            } else {
                MessageBoxW(h, L"保存失败（ini 写入错误）。", L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }
        case IDC_BTN_CLOSECFG:
            DestroyWindow(h);
            break;
        }
        break;

    case WM_CLOSE:
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
    int w = dpi_scale(540), hgt = dpi_scale(560);
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
        g_hmode  = MKCTL(L"RICHEDIT50W", 0, ES_READONLY, IDC_STC_MODE, L"○ 待机");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_START, L"开始");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_STOP, L"停止");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_FORMAT, L"格式化");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_READLABEL, L"读取标签");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_WRITELABEL, L"写入标签");
        MKCTL(L"BUTTON", 0, BS_PUSHBUTTON, IDC_BTN_CONFIG, L"配置");
        g_hchk_top = MKCTL(L"BUTTON", 0, BS_AUTOCHECKBOX, IDC_CHK_TOPMOST, L"窗口置顶");
        g_hlabel = MKCTL(L"STATIC", 0, SS_LEFT, IDC_STC_LABEL, L"标签：");
        g_hdev = MKCTL(L"STATIC", 0, SS_LEFT, IDC_STC_DEVICE, L"设备状态：检测中…");
        g_herr = MKCTL(L"RICHEDIT50W", 0, ES_READONLY, IDC_STC_ERROR, L"");
        g_hprog = MKCTL(PROGRESS_CLASSW, 0, PBS_SMOOTH, IDC_PBAR_TASK, L"");
        g_hcmd[0] = MKCTL(L"RICHEDIT50W", WS_EX_CLIENTEDGE,
                          ES_MULTILINE | ES_READONLY | WS_VSCROLL, IDC_EDIT_ENGINE, L"");
        g_hcmd[1] = MKCTL(L"RICHEDIT50W", WS_EX_CLIENTEDGE,
                          ES_MULTILINE | ES_READONLY | WS_VSCROLL, IDC_EDIT_IDENT, L"");
        g_hverdict = MKCTL(L"RICHEDIT50W", 0, ES_READONLY | ES_CENTER, IDC_STC_READVERDICT, L"");
        g_hfile = MKCTL(L"RICHEDIT50W", WS_EX_CLIENTEDGE,
                        ES_MULTILINE | ES_READONLY | WS_VSCROLL, IDC_EDIT_FILE, L"");
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

    case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        switch (id) {
        case IDC_COMBO_MODEL:
            if (code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(g_hmodel, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < g_cfg.nmodels && sel != g_cur_model) {
                    g_cur_model = sel;
                    applog(CH_ENGINE, LOG_UI, L"[UI] 机型切换为：%s（内部键 %s，与轮询解耦）",
                           g_cfg.models[sel].display, g_cfg.models[sel].internal);
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
            run_single(OP_LABELWRITE);
            break;
        case IDC_BTN_CONFIG:
            open_config_dialog();
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
    case WM_APP_PROGRESS:
        SendMessageW(g_hprog, PBM_SETPOS, wp, 0);
        return 0;
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
                    /* 机型 = 当前下拉框选择（与轮询解耦）；机型开启 AUTORUN 则自动执行 */
                    CFGMODEL* m = &g_cfg.models[g_cur_model];
                    if (cfg_get_int(m, L"AUTORUN", 0) && !g_running)
                        task_run_async(g_cur_model, TRIGGER_AUTO);
                } else {
                    applog(CH_ENGINE, LOG_UI, L"[DEV] 可移动磁盘 %c:\\ 已拔出", d);
                    SendMessageW(g_hprog, PBM_SETPOS, 0, 0);
                }
            }
        }
        return 0;
    }
    case WM_APP_VERDICT:
        set_verdict((wchar_t*)lp);
        free((void*)lp);
        return 0;
    case WM_APP_FILECONTENT:
        SetWindowTextW(g_hfile, (wchar_t*)lp ? (wchar_t*)lp : L"");
        free((void*)lp);
        return 0;
    case WM_APP_LABEL: {
        wchar_t txt[600];
        _snwprintf(txt, 600, L"标签：%s", (wchar_t*)lp ? (wchar_t*)lp : L"");
        SetWindowTextW(g_hlabel, txt);
        free((void*)lp);
        return 0;
    }
    case WM_APP_ERROR:
        set_error_badge(wp != 0, (wchar_t*)lp);
        free((void*)lp);
        return 0;
    case WM_APP_MODE:
        set_mode_badge((int)wp);
        return 0;
    case WM_APP_TASKDONE:
        InterlockedExchange(&g_running, 0);
        InterlockedExchange(&g_stop, 0);
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
    int W = dpi_scale(880), H = dpi_scale(640);
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

    /* 全局热键 T / C */
    RegisterHotKey(h, ID_HOTKEY_T, 0, 'T');
    RegisterHotKey(h, ID_HOTKEY_C, 0, 'C');

    applog(CH_ENGINE, LOG_UI, L"[SYS] %s v%s 启动（ini：%s）",
           g_cfg.title, g_cfg.version, g_ini_path);
    applog(CH_ENGINE, LOG_UI, L"[SYS] 已加载 %d 个机型，当前默认：%s",
           g_cfg.nmodels, g_cfg.models[g_cur_model].internal);
    if (!g_cfg.adminpass[0])
        applog(CH_ENGINE, LOG_WARN, L"[SYS] 未设置管理员密码，配置窗口仅只读");

    /* 启动设备轮询（机型匹配与轮询解耦） */
    dev_poll_start(h);

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
