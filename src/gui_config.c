/* ============================================================
 * gui_config.c - 配置窗口（WND_CONFIG / UDK_CFGCLASS）
 * 从 gui.c 拆出：键提示表、枚举/自由键值编辑器、只读锁定、
 * 保存（cfg_save_key）/恢复默认（write_default_ini）、cfg_proc、
 * 控件创建与窗口类注册。与 gui.c 的共享符号见 gui_shared.h。
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
#include "version.h"
#include "default_ini.h"

/* ============================================================
 * 配置窗口
 * ============================================================ */

static BOOL g_cfg_unlocked = FALSE;
HWND g_hcfglist = NULL, g_hcfgtext = NULL, g_hcfgpass = NULL,
            g_hcfgstate = NULL, g_hcfgkey = NULL, g_hcfgval = NULL,
            g_hcfgvalcombo = NULL, g_hcfghint = NULL, g_hcfgvallbl = NULL;

/* 键的简要说明（配置窗口提示行）：覆盖常用键的语义与取值说明；
 * BTN_ 前缀按钮名与未知键返回通用提示 */
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
        { L"READFILE",        L"读取文件：支持子目录与通配符，如 SYSTEM\\PAW6K-LSN、8685????.txt、SYSTEM\\*.txt；留空=跳过读取" },
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
 * from_list=TRUE 表示键名来自下拉列表选中（此时编辑框文本可能尚未刷新，
 * 必须用 CB_GETCURSEL/CB_GETLBTEXT 取列表项，否则拿到旧键名导致控件类型不切换）；
 * fill_value=TRUE 时把配置里的当前值回填到值控件 */
static void cfg_sync_value_editor_ex(BOOL from_list, BOOL fill_value)
{
    wchar_t key[CFG_KEY_LEN];
    if (from_list) {
        int ki = (int)SendMessageW(g_hcfgkey, CB_GETCURSEL, 0, 0);
        if (ki >= 0 &&
            SendMessageW(g_hcfgkey, CB_GETLBTEXT, ki, (LPARAM)key) != CB_ERR) {
            /* 键名取自列表项（新选中的键） */
        } else {
            GetWindowTextW(g_hcfgkey, key, CFG_KEY_LEN);
        }
    } else {
        GetWindowTextW(g_hcfgkey, key, CFG_KEY_LEN);   /* 手输键名（CBS_DROPDOWN） */
    }
    if (!key[0]) return;
    int mi = (int)SendMessageW(g_hcfglist, LB_GETCURSEL, 0, 0);
    if (mi < 0 || mi >= g_cfg.nmodels) return;

    SetWindowTextW(g_hcfghint, key_hint(key));   /* 键值说明提示（后文按锁定状态补充） */
    CFGMODEL* m = &g_cfg.models[mi];
    const wchar_t* cur = cfg_get(m, key);
    if (!cur) cur = L"";

    BOOL enum_bad = FALSE;   /* A5.2：枚举键的当前值不在可选列表（旧版/手改 ini） */
    const wchar_t* vals[4];
    int n = key_enum_values(key, vals, 4);
    if (n > 0) {
        SetWindowTextW(g_hcfgvallbl, L"新值（下拉选择）：");
        ShowWindow(g_hcfgvalcombo, SW_SHOW);
        ShowWindow(g_hcfgval, SW_HIDE);
        SendMessageW(g_hcfgvalcombo, CB_RESETCONTENT, 0, 0);
        int pick = -1;
        for (int i = 0; i < n; i++) {
            SendMessageW(g_hcfgvalcombo, CB_ADDSTRING, 0, (LPARAM)vals[i]);
            if (_wcsicmp(vals[i], cur) == 0) pick = i;
        }
        if (pick < 0) {
            /* 非法枚举值：安全回退——保留当前值显示并选中，不自动改写配置；
             * 用户改选合法值后才能保存（保存时写回的是下拉选中值）。 */
            enum_bad = TRUE;
            if (cur[0]) {
                SendMessageW(g_hcfgvalcombo, CB_ADDSTRING, 0, (LPARAM)cur);
                pick = n;
            } else {
                pick = 0;
            }
        }
        SendMessageW(g_hcfgvalcombo, CB_SETCURSEL, pick, 0);
    } else {
        /* 自由键（SRCPATH/DSTPATH/READFILE/READRULE/VOLUME/BTN_* 等）→ 输入框，
         * 无论锁定与否都回填当前值（锁定时灰显可看不可改），
         * 并顺带清掉上个枚举键可能残留在输入框里的旧值 */
        SetWindowTextW(g_hcfgvallbl, L"新值（输入）：");
        ShowWindow(g_hcfgvalcombo, SW_HIDE);
        ShowWindow(g_hcfgval, SW_SHOW);
        if (fill_value) SetWindowTextW(g_hcfgval, cur);
    }
    /* 说明行：未解锁时明示输入框为只读及解锁方法；非法枚举值额外提示保留原值 */
    const wchar_t* h = key_hint(key);
    wchar_t hintbuf[1024];
    if (!g_cfg_unlocked) {
        _snwprintf(hintbuf, 1024,
                   L"%s　——　锁定中：新值框灰色只读，点[解锁编辑]输入密码后可修改", h);
    } else if (enum_bad) {
        _snwprintf(hintbuf, 1024,
                   L"%s　——　当前值“%s”不在可选列表（保留原值，选择合法值后可保存）", h, cur);
    } else {
        lstrcpynW(hintbuf, h, 1024);
    }
    hintbuf[1023] = 0;
    SetWindowTextW(g_hcfghint, hintbuf);
    cfg_apply_lock();
}

/* cfg_sync_value_editor 的常用形式：按列表选中键切换值控件并回填当前值 */
static void cfg_sync_value_editor(void)
{
    cfg_sync_value_editor_ex(TRUE, TRUE);
}

/* 切换选中机型后刷新"配置内容"只读展示区与键名下拉；
 * 内容区恒为只读（真正编辑只通过"键名+新值"单键写回路径） */
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

/* 按解锁状态启用/禁用编辑控件与 [保存修改]/[恢复默认设置]；
 * 未解锁时窗口整体只读（无密码错误次数锁定，允许无限重试） */
static void cfg_apply_lock(void)
{
    EnableWindow(g_hcfgkey, g_cfg_unlocked);
    EnableWindow(g_hcfgval, g_cfg_unlocked);
    EnableWindow(g_hcfgvalcombo, g_cfg_unlocked && IsWindowVisible(g_hcfgvalcombo));
    /* A9：未解锁时禁用 [保存修改] 与 [恢复默认设置]（恢复默认同样要求编辑权限） */
    if (g_hcfg && IsWindow(g_hcfg)) {
        EnableWindow(GetDlgItem(g_hcfg, IDC_BTN_SAVECFG), g_cfg_unlocked);
        EnableWindow(GetDlgItem(g_hcfg, IDC_BTN_RESETCFG), g_cfg_unlocked);
    }
    SetWindowTextW(g_hcfgstate, g_cfg_unlocked ? L"状态：已解锁（可编辑保存）" : L"状态：只读（未授权）");
}

/* 配置窗口标题：随 TITLE/VERSION 刷新（L14）；窗口未打开时忽略 */
static void cfg_update_title(void)
{
    if (!g_hcfg || !IsWindow(g_hcfg)) return;
    wchar_t t[400];
    _snwprintf(t, 400, L"配置 — %s v%s（只读，解锁后可编辑）",
               g_cfg.title, g_cfg.version);
    t[399] = 0;
    SetWindowTextW(g_hcfg, t);
}

/* 重建配置窗口机型下拉列表并刷新“配置内容”显示；窗口未打开时忽略。
 * keep_internal 非空时按其恢复选中项，否则选中第一个（L14：保存/恢复默认后列表不刷新）。 */
void cfg_rebuild_model_list(const wchar_t* keep_internal)
{
    if (!g_hcfglist || !IsWindow(g_hcfglist)) return;
    SendMessageW(g_hcfglist, LB_RESETCONTENT, 0, 0);
    int newsel = 0;
    for (int i = 0; i < g_cfg.nmodels; i++) {
        SendMessageW(g_hcfglist, LB_ADDSTRING, 0, (LPARAM)g_cfg.models[i].display);
        if (keep_internal && keep_internal[0] &&
            _wcsicmp(g_cfg.models[i].internal, keep_internal) == 0)
            newsel = i;
    }
    if (g_cfg.nmodels > 0) SendMessageW(g_hcfglist, LB_SETCURSEL, newsel, 0);
    cfg_show_model();     /* 键列表 + 配置内容 + 新值控件 */
    cfg_update_title();
}
/* 用内嵌出厂默认配置覆盖 ini（P1-16：备份 + 原子替换 + 写后校验，失败保留原文件） */
static BOOL write_default_ini(void)
{
    static const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
    const char* body = DEFAULT_INI_UTF8;
    DWORD body_len = (DWORD)strlen(body);

    if (!g_ini_path[0]) {
        applog(CH_ENGINE, LOG_ERR, L"[CFG] 恢复默认设置失败：ini 路径为空");
        return FALSE;
    }

    /* 1) 先备份原 ini（失败仅告警，不阻断恢复流程） */
    wchar_t bak[MAX_PATH];
    _snwprintf(bak, MAX_PATH, L"%s.bak", g_ini_path);
    bak[MAX_PATH - 1] = 0;
    BOOL has_bak = FALSE;
    if (CopyFileW(g_ini_path, bak, FALSE)) {
        has_bak = TRUE;
        applog(CH_ENGINE, LOG_INFO, L"[CFG] 已备份原 ini：%s", bak);
    } else {
        DWORD berr = GetLastError();
        if (berr == ERROR_FILE_NOT_FOUND || berr == ERROR_PATH_NOT_FOUND)
            applog(CH_ENGINE, LOG_WARN, L"[CFG] 原 ini 不存在，跳过备份：%s", g_ini_path);
        else
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] 备份原 ini 失败（错误码 %lu），继续写默认配置：%s",
                   (unsigned long)berr, bak);
    }

    /* 2) PID 唯一临时文件：写 BOM + 内嵌默认配置，检查写入字节数并 FlushFileBuffers */
    wchar_t tmp[MAX_PATH];
    _snwprintf(tmp, MAX_PATH, L"%s.tmp.%lu", g_ini_path, (unsigned long)GetCurrentProcessId());
    tmp[MAX_PATH - 1] = 0;
    DeleteFileW(tmp);   /* 清理上次残留（若同名路径是目录则删除失败，CreateFile 会随之失败） */
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        applog(CH_ENGINE, LOG_ERR,
               L"[CFG] 恢复默认设置失败：无法创建临时文件 %s（错误码 %lu），原 ini 未修改",
               tmp, (unsigned long)GetLastError());
        return FALSE;
    }
    BOOL ok = TRUE;
    DWORD w = 0, werr = 0;
    if (!WriteFile(h, bom, 3, &w, NULL) || w != 3) {
        ok = FALSE; werr = GetLastError();
    }
    if (ok) {
        w = 0;
        if (!WriteFile(h, body, body_len, &w, NULL) || w != body_len) {
            ok = FALSE; werr = GetLastError();
        }
    }
    if (ok && !FlushFileBuffers(h)) { ok = FALSE; werr = GetLastError(); }
    if (!CloseHandle(h) && ok) { ok = FALSE; werr = GetLastError(); }
    if (!ok) {
        DeleteFileW(tmp);
        applog(CH_ENGINE, LOG_ERR,
               L"[CFG] 恢复默认设置失败：写临时文件失败（错误码 %lu），原 ini 未修改",
               (unsigned long)werr);
        return FALSE;
    }

    /* 3) 原子替换（REPLACE_EXISTING|WRITE_THROUGH） */
    if (!MoveFileExW(tmp, g_ini_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD merr = GetLastError();
        DeleteFileW(tmp);
        applog(CH_ENGINE, LOG_ERR,
               L"[CFG] 恢复默认设置失败：替换 ini 失败（错误码 %lu），原 ini 未修改",
               (unsigned long)merr);
        return FALSE;
    }

    /* 4) 写后 cfg_load 回读校验（堆分配，避免 906KB 栈帧）；失败则用备份还原 */
    CONFIG* chk = (CONFIG*)malloc(sizeof(CONFIG));
    if (!chk) {
        if (has_bak) CopyFileW(bak, g_ini_path, FALSE);
        applog(CH_ENGINE, LOG_ERR, L"[CFG] 恢复默认设置失败：内存不足，已还原原 ini");
        return FALSE;
    }
    BOOL parsed = cfg_load(g_ini_path, chk);
    free(chk);
    if (!parsed) {
        if (has_bak && CopyFileW(bak, g_ini_path, FALSE)) {
            applog(CH_ENGINE, LOG_ERR,
                   L"[CFG] 恢复默认设置失败：写入后的 ini 无法解析，已从备份还原：%s", bak);
        } else {
            DeleteFileW(g_ini_path);
            applog(CH_ENGINE, LOG_ERR,
                   L"[CFG] 恢复默认设置失败：写入后的 ini 无法解析，已删除该文件（原备份不可用）");
        }
        return FALSE;
    }
    return TRUE;
}

/* 创建配置窗口全部子控件（布局随 DPI 缩放；控件 ID 见 ui/resource.h 3xxx 段） */
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
    CFGSTC(IDC_STC_CFGVALLBL, L"新值（下拉选择）：", m, y + dpi_scale(4), dpi_scale(160), rowh);
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

/* A5.1：配置窗口初始选中的机型 = 主界面当前机型（按 internal 匹配），找不到回退 0 */
static int cfg_current_model_sel(void)
{
    if (g_cur_model >= 0 && g_cur_model < g_cfg.nmodels) {
        const wchar_t* cur = g_cfg.models[g_cur_model].internal;
        for (int i = 0; i < g_cfg.nmodels; i++)
            if (_wcsicmp(g_cfg.models[i].internal, cur) == 0) return i;
    }
    return 0;
}

/* 配置窗口过程：WM_CREATE 建控件并初始选中主界面机型；
 * 解锁编辑（ADMINPASS 明文比较，错误可无限重试）、单键保存（cfg_save_key
 * 保留编码/行尾/注释，写后回读校验）、恢复默认（write_default_ini 原子写）；
 * 关窗时重载配置并回只读后销毁窗口 */
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
        g_hcfgvallbl = GetDlgItem(h, IDC_STC_CFGVALLBL);
        for (int i = 0; i < g_cfg.nmodels; i++)
            SendMessageW(g_hcfglist, LB_ADDSTRING, 0, (LPARAM)g_cfg.models[i].display);
        /* A5.1：初始选中主界面当前机型（而不是固定第一个） */
        SendMessageW(g_hcfglist, LB_SETCURSEL, cfg_current_model_sel(), 0);
        cfg_show_model();
        cfg_apply_lock();
        /* 密码输入框：平时隐藏且禁用，点击"解锁编辑"后才显示 */
        ShowWindow(g_hcfgpass, SW_HIDE);
        EnableWindow(g_hcfgpass, FALSE);
        /* A5.2：输入长度受控（与 CFG_KEY_LEN/CFG_VAL_LEN 一致，避免超长输入） */
        SendMessageW(g_hcfgkey, CB_LIMITTEXT, CFG_KEY_LEN - 1, 0);
        SendMessageW(g_hcfgval, EM_SETLIMITTEXT, CFG_VAL_LEN - 1, 0);
        SendMessageW(g_hcfgvalcombo, CB_LIMITTEXT, CFG_VAL_LEN - 1, 0);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CFG_LIST_MODELS:
            if (HIWORD(wp) == LBN_SELCHANGE) cfg_show_model();
            break;
        case IDC_CFG_COMBO_KEY:
            if (HIWORD(wp) == CBN_SELCHANGE)
                cfg_sync_value_editor_ex(TRUE, TRUE);   /* 列表选择：以列表项键名为准，回填当前值 */
            else if (HIWORD(wp) == CBN_EDITCHANGE)
                cfg_sync_value_editor_ex(FALSE, TRUE);  /* 手输键名：以编辑框文本为准 */
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
                cfg_apply_lock();
                /* 恢复密码框隐藏 */
                ShowWindow(g_hcfgpass, SW_HIDE);
                EnableWindow(g_hcfgpass, FALSE);
                SetWindowTextW(GetDlgItem(h, IDC_BTN_UNLOCK), L"解锁编辑");
                applog(CH_ENGINE, LOG_UI, L"[CFG] 配置窗口已解锁编辑");
            } else {
                /* 用户要求：不做密码错误次数锁定，密码错误仅提示并可无限重试；
                 * 日志不记录密码内容。 */
                applog(CH_ENGINE, LOG_WARN, L"[CFG] 配置窗口解锁失败：密码错误（可重试）");
                MessageBoxW(h, L"密码错误，请重试。", L"解锁失败", MB_OK | MB_ICONWARNING);
                SetWindowTextW(g_hcfgpass, L"");
                SetFocus(g_hcfgpass);
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
            if (!cfg_save_key(g_ini_path, section, key, val)) {
                /* ini 未被修改：失败路径不得报成功 */
                applog(CH_ENGINE, LOG_ERR,
                       L"[CFG] 保存失败：ini 写入/回读校验未通过（%s.%s），原配置未修改",
                       section, key);
                MessageBoxW(h, L"保存失败（ini 写入或回读校验未通过），原配置未被修改。",
                            L"错误", MB_OK | MB_ICONERROR);
                break;
            }
            /* ini 已写入成功；重载失败必须显式报错，不得假报“配置已重载”（P1-16/L14） */
            if (!reload_config_and_ui()) {
                applog(CH_ENGINE, LOG_ERR,
                       L"[CFG] ini 已写入但重载失败（%s），界面可能仍显示旧配置；请检查 ini 或重启程序",
                       g_ini_path);
                MessageBoxW(h,
                            L"ini 已写入，但重新加载失败。\n界面可能仍显示旧配置，请检查 ini 文件或重启程序。",
                            L"重载失败", MB_OK | MB_ICONERROR);
                break;
            }
            /* 重载成功：主标题/配置窗口机型列表与内容已由 reload 内刷新，这里确保当前键值显示最新 */
            cfg_show_model();
            applog(CH_ENGINE, LOG_OK, L"[CFG] 已保存 %s.%s=%s（配置已重载）",
                   section, key, val);
            break;
        }
        case IDC_BTN_RESETCFG: {
            /* A9：未解锁时不允许恢复默认（按钮已置灰，此处再兜底校验） */
            if (!g_cfg_unlocked) {
                MessageBoxW(h, L"恢复默认设置需要先解锁编辑（只读模式不可用）。",
                            L"提示", MB_OK | MB_ICONINFORMATION);
                break;
            }
            if (MessageBoxW(h,
                    L"确认恢复默认设置？\n\n当前 UdiskTools.ini 将被出厂默认配置覆盖，\n自定义的机型与路径修改会丢失！",
                    L"恢复默认设置", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2) != IDOK)
                break;
            if (!write_default_ini()) {
                applog(CH_ENGINE, LOG_ERR,
                       L"[CFG] 恢复默认设置失败：默认配置写入/校验失败，原 ini 未修改");
                MessageBoxW(h,
                            L"恢复默认设置失败（默认配置写入或校验失败），原 ini 未被修改。",
                            L"错误", MB_OK | MB_ICONERROR);
                break;
            }
            /* 写后重载同样检查返回值（不得假报成功） */
            if (!reload_config_and_ui()) {
                applog(CH_ENGINE, LOG_ERR,
                       L"[CFG] 默认配置已写入但重载失败（%s）；请检查 ini 或重启程序", g_ini_path);
                MessageBoxW(h,
                            L"默认配置已写入，但重新加载失败。\n请检查 ini 文件或重启程序。",
                            L"重载失败", MB_OK | MB_ICONERROR);
                break;
            }
            g_cfg_unlocked = FALSE;
            cfg_apply_lock();
            /* 重建机型列表与内容显示（与原行为一致：恢复默认后选中第一个机型） */
            if (g_hcfglist && IsWindow(g_hcfglist)) {
                SendMessageW(g_hcfglist, LB_SETCURSEL, 0, 0);
                cfg_show_model();
            }
            cfg_update_title();
            /* 备份位置提示（存在才提示，日志） */
            {
                wchar_t bak[MAX_PATH];
                _snwprintf(bak, MAX_PATH, L"%s.bak", g_ini_path);
                bak[MAX_PATH - 1] = 0;
                if (GetFileAttributesW(bak) != INVALID_FILE_ATTRIBUTES)
                    applog(CH_ENGINE, LOG_INFO, L"[CFG] 原 ini 备份位置：%s", bak);
            }
            applog(CH_ENGINE, LOG_UI, L"[CFG] 已恢复默认设置（内嵌出厂配置覆盖并重载）");
            MessageBoxW(h, L"已恢复默认设置并立即重载。", L"提示", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case IDC_BTN_CLOSECFG:
            /* 保存/修改后关闭时也确保配置已重载；失败不得静默 */
            if (!reload_config_and_ui())
                applog(CH_ENGINE, LOG_WARN,
                       L"[CFG] 关闭配置窗口时重载配置失败，界面仍使用旧配置");
            g_cfg_unlocked = FALSE;   /* A5：关窗后回到只读 */
            cfg_apply_lock();
            DestroyWindow(h);
            break;
        }
        break;

    case WM_CLOSE:
        /* 关闭配置窗口时重载配置（覆盖外部修改并刷新主界面）；失败不得静默 */
        if (!reload_config_and_ui())
            applog(CH_ENGINE, LOG_WARN,
                   L"[CFG] 关闭配置窗口时重载配置失败，界面仍使用旧配置");
        g_cfg_unlocked = FALSE;   /* A5：关窗后回到只读 */
        cfg_apply_lock();
        DestroyWindow(h);
        return 0;
    case WM_NCDESTROY:
        g_hcfg = NULL;
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* 打开配置窗口（主窗[配置]按钮入口）：已打开则置前台；每次打开回只读 */
void open_config_dialog(void)
{
    if (g_hcfg && IsWindow(g_hcfg)) { SetForegroundWindow(g_hcfg); return; }
    g_cfg_unlocked = FALSE;
    int w = dpi_scale(540), hgt = dpi_scale(620);
    CreateWindowExW(WS_EX_DLGMODALFRAME, L"UDK_CFGCLASS",
                    L"配置",
                    (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) | WS_VISIBLE,
                    CW_USEDEFAULT, CW_USEDEFAULT, w, hgt,
                    g_hmain, NULL, g_hinst, NULL);
    cfg_update_title();   /* L14：配置窗口标题随 TITLE/VERSION */
}
/* 注册配置窗口类（gui_run 启动时调用一次） */
void cfg_register_class(HINSTANCE hInst)
{
    WNDCLASSW wc2;
    memset(&wc2, 0, sizeof(wc2));
    wc2.lpfnWndProc = cfg_proc;
    wc2.hInstance = hInst;
    wc2.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc2.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc2.lpszClassName = L"UDK_CFGCLASS";
    wc2.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc2);
}
