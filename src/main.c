/* ============================================================
 * UdiskTools - main.c
 * 程序入口：加载 ini → 初始化 GUI → 设备轮询 → 操作引擎
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "gui.h"
#include "config.h"
#include "util.h"
#include "version.h"

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR pCmdLine, int nShow)
{
    (void)hPrev;
    (void)pCmdLine;

    /* ini 路径：exe 同目录 UdiskTools.ini（日志强制输出，路径就绪后立即可用） */
    path_join(g_ini_path, MAX_PATH, exe_dir(), L"UdiskTools.ini");

    filelog(LOG_UI, L"==== 程序启动 v%s ====", APP_VERSION_W);

    if (!cfg_load(g_ini_path, &g_cfg)) {
        wchar_t msg[600];
        _snwprintf(msg, 600,
                   L"无法加载配置文件：\n%s\n\n"
                   L"请确认 UdiskTools.ini 与程序位于同一目录且包含至少一个机型节。",
                   g_ini_path);
        filelog(LOG_ERR, L"ini 加载失败：%s", g_ini_path);
        MessageBoxW(NULL, msg, L"UdiskTools - 配置错误", MB_OK | MB_ICONERROR);
        return 1;
    }
    filelog(LOG_OK, L"ini 加载成功：%d 个机型（默认 %s）",
            g_cfg.nmodels, g_cfg.defaultmodel);

    int rc = gui_run(hInst, nShow);

    filelog(LOG_UI, L"==== 程序退出（rc=%d） ====", rc);
    return rc;
}
