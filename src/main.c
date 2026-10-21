/* ============================================================
 * UdiskTools - main.c
 * 程序入口骨架（Phase 1 起逐步填充：ini 解析、设备轮询、操作引擎）
 * ============================================================ */
#include <windows.h>

#include "version.h"

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR pCmdLine, int nShow)
{
    (void)hInst; (void)hPrev; (void)pCmdLine; (void)nShow;

    /* TODO(Phase 2): Per-Monitor V2 DPI 感知（manifest + SetProcessDpiAwarenessContext）
     * TODO(Phase 1): 加载 ini（TITLE/VERSION/机型节）→ 输出启动日志
     * TODO(Phase 1): 1s 轮询 DRIVE_REMOVABLE → 匹配当前选中机型 → 执行操作
     * TODO(Phase 2): WND_MAIN（PANE_TOP/STATUS/LABEL/TASK/CMDS/FILE）消息循环
     */
    MessageBoxW(NULL,
        L"UdiskTools 骨架程序。\n详细规划见 docs/软件规划书.md",
        APP_TITLE_W L" v" APP_VERSION_W,
        MB_OK | MB_ICONINFORMATION);
    return 0;
}
