/* ============================================================
 * device.c - 可移动磁盘轮询实现（1s 轮询，read-U 已验证方案）
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "device.h"

static HANDLE g_thread = NULL;
static volatile BOOL g_run = FALSE;
static HWND g_hwnd = NULL;

int dev_enum_removable(DWORD* mask, DEVINFO* infos)
{
    *mask = 0;
    int count = 0;
    for (wchar_t d = L'C'; d <= L'Z'; d++) {
        wchar_t root[8];
        _snwprintf(root, 8, L"%c:\\", d);
        if (GetDriveTypeW(root) == DRIVE_REMOVABLE) {
            *mask |= (1u << (d - L'A'));
            if (infos && count < DEV_MAX_DRIVES)
                dev_get_info(d, &infos[count]);
            count++;
        }
    }
    return count;
}

BOOL dev_get_info(wchar_t letter, DEVINFO* out)
{
    memset(out, 0, sizeof(DEVINFO));
    out->letter = letter;
    _snwprintf(out->root, 8, L"%c:\\", letter);

    DWORD volser = 0, maxcomp = 0, fsflags = 0;
    if (GetVolumeInformationW(out->root, out->volume, 128,
                              &volser, &maxcomp, &fsflags,
                              out->fs, 32)) {
        out->serial = volser;
        out->has_serial = TRUE;
    } else {
        out->volume[0] = 0;
        lstrcpynW(out->fs, L"未知", 32);
    }

    ULARGE_INTEGER total, freeq;
    if (GetDiskFreeSpaceExW(out->root, NULL, &total, &freeq)) {
        out->total_bytes = total.QuadPart;
        out->free_bytes = freeq.QuadPart;
    }
    return TRUE;
}

/* ---------- 轮询线程 ---------- */

static DWORD WINAPI poll_thread(LPVOID param)
{
    (void)param;
    DWORD prev_mask = 0;
    BOOL first = TRUE;

    while (g_run) {
        DWORD mask = 0;
        dev_enum_removable(&mask, NULL);

        if (!first && mask != prev_mask) {
            DWORD inserted = mask & ~prev_mask;
            DWORD removed = prev_mask & ~mask;
            /* 先报拔出再报插入 */
            if (removed)
                PostMessageW(g_hwnd, WM_APP_DEVCOUNT, (WPARAM)mask, (LPARAM)0);
            if (inserted)
                PostMessageW(g_hwnd, WM_APP_DEVCOUNT, (WPARAM)mask, (LPARAM)1);
        }
        prev_mask = mask;
        first = FALSE;

        for (int i = 0; i < 10 && g_run; i++)
            Sleep(100);   /* 1s，可中断 */
    }
    return 0;
}

BOOL dev_poll_start(HWND hwnd)
{
    if (g_thread) return TRUE;
    g_hwnd = hwnd;
    g_run = TRUE;
    g_thread = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
    return g_thread != NULL;
}

void dev_poll_stop(void)
{
    g_run = FALSE;
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}
