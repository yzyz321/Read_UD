/* ============================================================
 * UdiskTools - main.c
 * 程序入口：命令行（--version/--help/--check-ini/--ini）→ 崩溃转储
 *           → 单实例 → 加载 ini → 初始化 GUI
 *
 * 注意（GUI 子系统）：本程序用 -mwindows 链接，正常情况下没有控制台。
 * CLI 输出优先 AttachConsole(ATTACH_PARENT_PROCESS)/重定向句柄，
 * 两者都不可用时才用 MessageBox 兜底。
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gui.h"
#include "config.h"
#include "util.h"
#include "version.h"
#include "default_ini.h"   /* 内置默认配置（ini 缺失时启动 / 恢复默认共用，见 default_ini.h） */

/* B 线 gui.h 若已提供窗口类宏（UDK_MAIN_WNDCLASS）则优先使用；
 * 未就绪时回退到 gui.c 当前注册的类名，避免与 B 线并发修改冲突。 */
#ifndef UDK_MAIN_WNDCLASS
#define UDK_MAIN_WNDCLASS L"UDK_MAINCLASS"
#endif

/* 单实例互斥量：Local\ 作用域（本机当前登录会话；不同用户互不影响） */
#define UDK_MUTEX_NAME L"Local\\UdiskTools.SingleInstance"

/* ============================================================
 * 命令行输出
 * ============================================================ */
static HANDLE g_cli_out = INVALID_HANDLE_VALUE;
static BOOL   g_cli_is_console = FALSE;

/* 打开输出通道：附加父控制台 → 重定向的 stdout 句柄 → CONOUT$。
 * 返回 FALSE 表示没有任何可写通道（由调用方决定是否 MessageBox 兜底）。 */
static BOOL cli_open_output(void)
{
    if (g_cli_out != INVALID_HANDLE_VALUE) return TRUE;

    /* GUI 子系统进程默认无控制台：先尝试附加调用方（cmd/pwsh）的控制台。
     * stdout 已被重定向（管道/文件）时即使附加失败也不影响后续写句柄。 */
    AttachConsole(ATTACH_PARENT_PROCESS);

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE) {
        /* 交互式控制台：打开当前控制台输出（AttachConsole 成功后 std 句柄可能仍无效） */
        h = CreateFileW(L"CONOUT$", GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    }
    if (h == NULL || h == INVALID_HANDLE_VALUE) return FALSE;

    g_cli_out = h;
    g_cli_is_console = (GetFileType(h) == FILE_TYPE_CHAR);
    return TRUE;
}

/* 写一段文本：控制台用 WriteConsoleW；重定向/文件按 UTF-8 写字节 */
static BOOL cli_write(const wchar_t* s)
{
    if (!s || !s[0]) return TRUE;
    if (!cli_open_output()) return FALSE;

    int len = (int)wcslen(s);
    if (g_cli_is_console) {
        DWORD written = 0;
        return WriteConsoleW(g_cli_out, s, (DWORD)len, &written, NULL) != FALSE;
    }

    int need = WideCharToMultiByte(CP_UTF8, 0, s, len, NULL, 0, NULL, NULL);
    if (need <= 0) return FALSE;
    char* buf = (char*)malloc((size_t)need);
    if (!buf) return FALSE;
    int got = WideCharToMultiByte(CP_UTF8, 0, s, len, buf, need, NULL, NULL);
    DWORD written = 0;
    BOOL ok = (got == need) &&
              WriteFile(g_cli_out, buf, (DWORD)got, &written, NULL) &&
              written == (DWORD)got;
    free(buf);
    return ok;
}

/* 输出 CLI 文本；无可用输出通道时退回 MessageBox（GUI 交互场景） */
static void cli_report(const wchar_t* text)
{
    if (!cli_write(text))
        MessageBoxW(NULL, text, L"UdiskTools", MB_OK | MB_ICONINFORMATION);
}

/* ============================================================
 * 命令行参数解析
 * ============================================================ */
typedef struct {
    BOOL version;                 /* --version / -v */
    BOOL help;                    /* --help / -h / -? */
    BOOL check_ini;               /* --check-ini */
    wchar_t ini_override[MAX_PATH]; /* --ini <path>（原样保留，稍后解析为绝对路径） */
} CLIOPTS;

static void cli_parse(CLIOPTS* o)
{
    memset(o, 0, sizeof(*o));

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        filelog(LOG_WARN, L"命令行解析失败（错误码 %lu），按无参数启动 GUI", GetLastError());
        return;
    }

    for (int i = 1; i < argc; i++) {
        const wchar_t* a = argv[i];
        if (!a || !a[0]) continue;

        if (_wcsicmp(a, L"--version") == 0 || _wcsicmp(a, L"-v") == 0) {
            o->version = TRUE;
        } else if (_wcsicmp(a, L"--help") == 0 || _wcsicmp(a, L"-h") == 0 ||
                   wcscmp(a, L"-?") == 0) {
            o->help = TRUE;
        } else if (_wcsicmp(a, L"--check-ini") == 0) {
            o->check_ini = TRUE;
        } else if (_wcsicmp(a, L"--ini") == 0) {
            if (i + 1 < argc && argv[i + 1][0]) {
                lstrcpynW(o->ini_override, argv[++i], MAX_PATH);
            } else {
                filelog(LOG_WARN, L"命令行参数 --ini 缺少路径，忽略该参数");
            }
        } else {
            /* 未知参数不阻断 GUI 启动（仅记录警告） */
            filelog(LOG_WARN, L"未知命令行参数：%s（忽略并继续）", a);
        }
    }
    LocalFree(argv);
}

/* ---------- --version / --help 文本 ---------- */

/* 构建日期/时间：APP_BUILD_DATE/TIME 为窄字符串（编译器预定义宏），
 * 统一转成宽字符供 _snwprintf 的 %s 使用，避免各处重复转换。 */
static void build_stamp(wchar_t* date, size_t datecch, wchar_t* tm, size_t tmcch)
{
    if (date && datecch) {
        date[0] = 0;
        MultiByteToWideChar(CP_ACP, 0, APP_BUILD_DATE, -1, date, (int)datecch);
        date[datecch - 1] = 0;
    }
    if (tm && tmcch) {
        tm[0] = 0;
        MultiByteToWideChar(CP_ACP, 0, APP_BUILD_TIME, -1, tm, (int)tmcch);
        tm[tmcch - 1] = 0;
    }
}

static void put_version_text(void)
{
    wchar_t date[64], tm[64], text[900];
    build_stamp(date, 64, tm, 64);
    _snwprintf(text, 900,
               L"%s 软件 v%s（FileVersion %d.%d.%d.%d）\n"
               L"构建时间：%s %s\n"
               L"配置版本：见配置文件 [TOOLSINFO] VERSION（--check-ini 可查看）\n"
               L"可选：--ini <path> / --check-ini / --help\n",
               APP_TITLE_W, APP_VERSION_W,
               APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH, APP_VERSION_BUILD,
               date, tm);
    text[899] = 0;
    cli_report(text);
}

static void put_help_text(void)
{
    wchar_t text[1200];
    _snwprintf(text, 1200,
               L"%s %s - %s\n\n"
               L"用法：udisktools.exe [选项]\n\n"
               L"  --ini <path>      指定配置文件（默认：exe 目录\\UdiskTools.ini；\n"
               L"                    支持引号与相对路径，相对路径按当前工作目录解析）\n"
               L"  --check-ini       仅加载校验配置文件并退出（0=成功，1=失败；不启动 GUI）\n"
               L"  --version, -v     显示版本与构建信息并退出\n"
               L"  --help, -h, -?    显示本帮助并退出\n\n"
               L"无选项时启动 GUI。\n",
               APP_TITLE_W, APP_VERSION_W, APP_TITLE_W);
    text[1199] = 0;
    cli_report(text);
}

/* ---------- --check-ini：不建 GUI，加载 ini 并输出结果（0/1） ---------- */
static int run_check_ini(void)
{
    wchar_t text[1200];
    if (cfg_load(g_ini_path, &g_cfg)) {
        _snwprintf(text, 1200,
                   L"ini 加载成功：%d 个机型（默认 %s，配置版本 %s）\n配置文件：%s\n",
                   g_cfg.nmodels, g_cfg.defaultmodel,
                   g_cfg.version[0] ? g_cfg.version : L"未设置", g_ini_path);
        text[1199] = 0;
        filelog(LOG_OK, L"[CLI] --check-ini 成功：%d 个机型（默认 %s，配置版本 %s）：%s",
                g_cfg.nmodels, g_cfg.defaultmodel,
                g_cfg.version[0] ? g_cfg.version : L"未设置", g_ini_path);
        cli_report(text);
        return 0;
    }

    DWORD attr = GetFileAttributesW(g_ini_path);
    const wchar_t* reason = (attr == INVALID_FILE_ATTRIBUTES)
                                ? L"文件不存在或不可访问"
                                : L"内容/格式错误（详见日志）";
    _snwprintf(text, 1200, L"ini 加载失败：%s\n原因：%s\n", g_ini_path, reason);
    text[1199] = 0;
    filelog(LOG_ERR, L"[CLI] --check-ini 失败：%s（%s）", g_ini_path, reason);
    cli_report(text);
    return 1;
}

/* ============================================================
 * 崩溃转储（SetUnhandledExceptionFilter + MiniDumpWriteDump）
 *  - 生成 logs\crash_YYYYMMDD_HHMMSS.dmp（MiniDumpWithDataSegs）
 *  - 日志记录异常码/地址/模块/转储路径
 *  - 返回 EXCEPTION_CONTINUE_SEARCH：不吞异常，进程仍按系统默认方式终止
 *  - 无“崩溃测试”开关：仅真实未处理异常触发
 * ============================================================ */
static LONG WINAPI crash_dump_filter(EXCEPTION_POINTERS* ep)
{
    static LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0)
        return EXCEPTION_CONTINUE_SEARCH;   /* 防递归（转储过程再次崩溃） */

    DWORD code = 0;
    void* addr = NULL;
    if (ep && ep->ExceptionRecord) {
        code = ep->ExceptionRecord->ExceptionCode;
        addr = ep->ExceptionRecord->ExceptionAddress;
    }

    /* 文件名：logs\crash_YYYYMMDD_HHMMSS.dmp */
    __time64_t t = _time64(NULL);
    struct tm lt;
    _localtime64_s(&lt, &t);
    wchar_t logdir[MAX_PATH], dmp[MAX_PATH];
    path_join(logdir, MAX_PATH, exe_dir(), L"logs");
    util_ensure_dirs(logdir);
    _snwprintf(dmp, MAX_PATH, L"%s\\crash_%04d%02d%02d_%02d%02d%02d.dmp",
               logdir, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
               lt.tm_hour, lt.tm_min, lt.tm_sec);
    dmp[MAX_PATH - 1] = 0;

    /* 异常地址所属模块 */
    wchar_t modname[MAX_PATH] = L"未知";
    HMODULE mod = NULL;
    if (addr &&
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)addr, &mod) && mod) {
        GetModuleFileNameW(mod, modname, MAX_PATH);
        modname[MAX_PATH - 1] = 0;
    }

    BOOL dumped = FALSE;
    HANDLE hf = CreateFileW(dmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers = FALSE;
        dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hf,
                                   MiniDumpWithDataSegs, &mei, NULL, NULL) != FALSE;
        CloseHandle(hf);
        if (!dumped) DeleteFileW(dmp);
    }
    DWORD dump_err = dumped ? 0 : GetLastError();

    if (dumped)
        filelog(LOG_ERR,
                L"[CRASH] 未处理异常 0x%08lX 地址 0x%016llX 模块 %s；已生成转储：%s",
                (unsigned long)code, (unsigned long long)(ULONG_PTR)addr, modname, dmp);
    else
        filelog(LOG_ERR,
                L"[CRASH] 未处理异常 0x%08lX 地址 0x%016llX 模块 %s；转储写入失败"
                L"（错误码 %lu，路径 %s）",
                (unsigned long)code, (unsigned long long)(ULONG_PTR)addr, modname,
                (unsigned long)dump_err, dmp);

    return EXCEPTION_CONTINUE_SEARCH;   /* 不吞异常 */
}

/* ============================================================
 * 单实例（CreateMutex + FindWindow 激活已有窗口）
 * ============================================================ */
static HANDLE g_instance_mutex = NULL;

static void release_instance_mutex(void)
{
    if (g_instance_mutex) {
        CloseHandle(g_instance_mutex);
        g_instance_mutex = NULL;
    }
}

/* 返回 1=继续启动；0=已有实例（本次直接退出，退出码 0） */
static int check_single_instance(void)
{
    g_instance_mutex = CreateMutexW(NULL, FALSE, UDK_MUTEX_NAME);
    if (!g_instance_mutex) {
        /* 创建失败（极少见）时不阻断启动，只记录警告 */
        filelog(LOG_WARN, L"无法创建单实例互斥量（错误码 %lu），按无单实例限制继续",
                GetLastError());
        return 1;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) return 1;

    /* 已有实例：非交互激活，不弹 MessageBox（避免非交互环境阻塞） */
    HWND existing = FindWindowW(UDK_MAIN_WNDCLASS, NULL);
    if (existing) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        filelog(LOG_INFO, L"已有实例在运行，已激活现有窗口（类名 %s），本次启动退出",
                UDK_MAIN_WNDCLASS);
    } else {
        /* 已有实例但窗口还没建好（启动中/正在退出）：只记警告，不等待 */
        filelog(LOG_WARN,
                L"已有实例在运行，但未找到主窗口（可能正在启动），本次启动退出");
    }
    release_instance_mutex();
    return 0;
}

/* 把内置默认配置（DEFAULT_INI_UTF8，来源 profiles/UdiskTools_default.ini）
 * 写入 path：临时文件 + FlushFileBuffers + MoveFileEx 原子替换。
 * 供"ini 不存在 → 用户选择以默认配置运行"使用（写盘后配置窗口保存/重载才可用）。
 * 返回 TRUE=写入并替换成功；FALSE=任何一步失败（不残留临时文件）。 */
static BOOL write_builtin_default_ini(const wchar_t* path)
{
    wchar_t tmp[MAX_PATH];
    _snwprintf(tmp, MAX_PATH, L"%s.tmp.%lu", path, (unsigned long)GetCurrentProcessId());
    tmp[MAX_PATH - 1] = 0;

    HANDLE f = CreateFileW(tmp, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    size_t len = strlen(DEFAULT_INI_UTF8);
    DWORD written = 0;
    BOOL ok = WriteFile(f, DEFAULT_INI_UTF8, (DWORD)len, &written, NULL)
              && written == (DWORD)len;
    if (ok && !FlushFileBuffers(f)) ok = FALSE;
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp); return FALSE; }

    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp);
        return FALSE;
    }
    return TRUE;
}

/* ============================================================
 * 入口
 * ============================================================ */
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR pCmdLine, int nShow)
{
    (void)hPrev;
    (void)pCmdLine;   /* 统一用 CommandLineToArgvW 解析（正确处理引号/路径） */

    /* ini 路径：exe 同目录 UdiskTools.ini（CLI 输出与日志都需要该路径） */
    path_join(g_ini_path, MAX_PATH, exe_dir(), L"UdiskTools.ini");

    /* ---------- 1) 命令行（CLI 路径不建 GUI、不建互斥量） ---------- */
    CLIOPTS opts;
    cli_parse(&opts);
    if (opts.ini_override[0]) {
        wchar_t full[MAX_PATH];
        DWORD n = GetFullPathNameW(opts.ini_override, MAX_PATH, full, NULL);
        if (n > 0 && n < MAX_PATH)
            lstrcpynW(g_ini_path, full, MAX_PATH);
        else
            filelog(LOG_WARN, L"无法解析 --ini 路径：%s（改用默认 %s）",
                    opts.ini_override, g_ini_path);
    }
    if (opts.help)    { put_help_text();    log_shutdown(); return 0; }
    if (opts.version) { put_version_text(); log_shutdown(); return 0; }
    if (opts.check_ini) {
        int cr = run_check_ini();
        log_shutdown();
        return cr;
    }

    /* ---------- 2) 崩溃转储：尽早安装，覆盖 ini 加载与 GUI 运行期 ---------- */
    SetUnhandledExceptionFilter(crash_dump_filter);

    /* 启动日志带构建日期/时间（P3-7：与 --version 共用 APP_BUILD_* 宏） */
    {
        wchar_t bdate[64], btime[64];
        build_stamp(bdate, 64, btime, 64);
        filelog(LOG_UI, L"==== 程序启动 软件 v%s（build %s %s） ====",
                APP_VERSION_W, bdate, btime);
    }

    /* ---------- 3) 单实例（正常实例保持互斥量句柄到退出） ---------- */
    if (!check_single_instance()) {
        log_shutdown();
        return 0;
    }

    /* ---------- 4) 加载 ini ---------- */
    if (!cfg_load(g_ini_path, &g_cfg)) {
        DWORD attr = GetFileAttributesW(g_ini_path);
        BOOL not_exist = (attr == INVALID_FILE_ATTRIBUTES)
                         && (GetLastError() == ERROR_FILE_NOT_FOUND
                             || GetLastError() == ERROR_PATH_NOT_FOUND);
        if (not_exist) {
            /* 配置文件不存在：由用户选择以内置默认配置运行，还是退出 */
            wchar_t msg[700];
            _snwprintf(msg, 700,
                       L"未找到配置文件：\n%s\n\n"
                       L"是（Y）：以内置默认配置运行\n"
                       L"（将在程序目录生成 UdiskTools.ini，可随时在配置窗口修改）\n"
                       L"否（N）：退出程序",
                       g_ini_path);
            msg[699] = 0;
            filelog(LOG_WARN, L"配置文件不存在：%s（等待用户选择）", g_ini_path);
            int id = MessageBoxW(NULL, msg, L"UdiskTools - 配置文件不存在",
                                 MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
            if (id != IDYES) {
                filelog(LOG_UI, L"用户选择：不以内置默认配置运行，程序退出");
                release_instance_mutex();
                log_shutdown();
                return 1;
            }
            filelog(LOG_UI, L"用户选择：以内置默认配置运行（写入 %s）", g_ini_path);
            if (!write_builtin_default_ini(g_ini_path) || !cfg_load(g_ini_path, &g_cfg)) {
                wchar_t msg2[600];
                _snwprintf(msg2, 600,
                           L"以内置默认配置启动失败：\n%s\n\n"
                           L"请检查程序目录写入权限，或手动将 UdiskTools.ini 放入该目录。",
                           g_ini_path);
                msg2[599] = 0;
                filelog(LOG_ERR, L"内置默认配置写入或加载失败：%s", g_ini_path);
                MessageBoxW(NULL, msg2, L"UdiskTools - 配置错误", MB_OK | MB_ICONERROR);
                release_instance_mutex();
                log_shutdown();
                return 1;
            }
            filelog(LOG_OK, L"已以内置默认配置运行（已生成：%s）", g_ini_path);
        } else {
            /* 文件存在但无法解析：维持原有错误退出路径 */
            wchar_t msg[600];
            _snwprintf(msg, 600,
                       L"无法加载配置文件：\n%s\n\n"
                       L"请确认 UdiskTools.ini 与程序位于同一目录且包含至少一个机型节。",
                       g_ini_path);
            filelog(LOG_ERR, L"ini 加载失败：%s", g_ini_path);
            MessageBoxW(NULL, msg, L"UdiskTools - 配置错误", MB_OK | MB_ICONERROR);
            release_instance_mutex();
            log_shutdown();
            return 1;
        }
    }
    filelog(LOG_OK, L"ini 加载成功：%d 个机型（默认 %s）",
            g_cfg.nmodels, g_cfg.defaultmodel);
    filelog(LOG_INFO, L"配置版本：%s（软件 v%s）",
            g_cfg.version[0] ? g_cfg.version : L"未设置", APP_VERSION_W);

    /* ---------- 5) GUI ---------- */
    int rc = gui_run(hInst, nShow);

    filelog(LOG_UI, L"==== 程序退出（rc=%d） ====", rc);

    /* 日志收尾（A 线 util.h：关闭文件日志缓存句柄，可重复调用） */
    log_shutdown();

    release_instance_mutex();
    return rc;
}
