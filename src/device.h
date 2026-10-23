/* ============================================================
 * device.h - 可移动磁盘轮询与信息
 * ============================================================ */
#ifndef UDK_DEVICE_H
#define UDK_DEVICE_H

#include <windows.h>

#define DEV_MAX_DRIVES 26

typedef struct {
    wchar_t  letter;            /* 'D' ... */
    wchar_t  root[8];           /* L"D:\\" */
    wchar_t  volume[128];       /* 卷标 */
    wchar_t  fs[32];            /* 文件系统 FAT32/EXFAT/... */
    DWORD    serial;            /* 卷序列号 */
    BOOL     has_serial;
    wchar_t  vidpid[64];        /* USB 硬件标识，如 "VID:0951 PID:1666"（区分同型号个体） */
    wchar_t  usbserial[64];     /* USB 序列号（同型号个体唯一） */
    unsigned __int64 total_bytes;
    unsigned __int64 free_bytes;
} DEVINFO;

/* 枚举当前所有可移动磁盘，返回数量，mask 的 bit(n-'A') 置位 */
int dev_enum_removable(DWORD* mask, DEVINFO* infos /* 可为 NULL，至多 26 */);

/* 取单个盘的详细信息（失败返回 FALSE） */
BOOL dev_get_info(wchar_t letter, DEVINFO* out);

/* 轮询线程：每 1s 枚举，变化时向 hwnd 投递 WM_APP_DEVCOUNT
 *   wparam = 当前 mask（bit 位表盘符），lparam = 变化类型 1=插入盘符 0=拔出盘符 */
#define WM_APP_DEVCOUNT (WM_APP + 3)

BOOL dev_poll_start(HWND hwnd);
void dev_poll_stop(void);

#endif
