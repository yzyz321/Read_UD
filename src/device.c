/* ============================================================
 * device.c - 可移动磁盘轮询实现（1s 轮询，read-U 已验证方案）
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include "device.h"

/* GUID_DEVINTERFACE_DISK {53F56307-B6BF-11D0-94F2-00A0C91EFB8B} */
DEFINE_GUID(UDK_GUID_DISK, 0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2,
            0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

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

/* ---------- USB VID/PID/序列号（区分同型号的不同个体） ----------
 * 盘符 → IOCTL_STORAGE_GET_DEVICE_NUMBER 找到物理磁盘
 *      → 磁盘设备实例 CM_Get_Parent 两级（Disk → USBSTOR → USB）
 *      → USB 设备 ID 解析 VID_/PID_，USBSTOR ID 提取序列号 */
static BOOL dev_get_usbid(wchar_t letter, wchar_t* vidpid, int cchvp,
                          wchar_t* usbserial, int cchsn)
{
    vidpid[0] = 0;
    usbserial[0] = 0;

    /* 1) 盘符 → 物理磁盘号 */
    wchar_t vpath[16];
    _snwprintf(vpath, 16, L"\\\\.\\%c:", letter);
    HANDLE hv = CreateFileW(vpath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL, OPEN_EXISTING, 0, NULL);
    if (hv == INVALID_HANDLE_VALUE) return FALSE;
    STORAGE_DEVICE_NUMBER sdn;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(hv, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,
                              &sdn, sizeof(sdn), &br, NULL);
    CloseHandle(hv);
    if (!ok) return FALSE;
    int disknum = (int)sdn.DeviceNumber;

    /* 2) 枚举磁盘接口，按磁盘号匹配设备实例 */
    HDEVINFO hset = SetupDiGetClassDevsW(&UDK_GUID_DISK, NULL, NULL,
                                         DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hset == INVALID_HANDLE_VALUE) return FALSE;

    BOOL found = FALSE;
    wchar_t usbstor_id[512] = L"";
    for (int i = 0; !found; i++) {
        SP_DEVICE_INTERFACE_DATA dif;
        dif.cbSize = sizeof(dif);
        if (!SetupDiEnumDeviceInterfaces(hset, NULL, &UDK_GUID_DISK, i, &dif)) break;

        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(hset, &dif, NULL, 0, &need, NULL);
        if (need == 0 || need > 1024) continue;
        unsigned char buf[1024];
        SP_DEVICE_INTERFACE_DETAIL_DATA_W* dd = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf;
        dd->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);   /* 1 字节成员对齐 */
        SP_DEVINFO_DATA devidata;
        devidata.cbSize = sizeof(devidata);
        if (!SetupDiGetDeviceInterfaceDetailW(hset, &dif, dd, sizeof(buf), NULL, &devidata))
            continue;

        HANDLE hd = CreateFileW(dd->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
        if (hd == INVALID_HANDLE_VALUE) continue;
        STORAGE_DEVICE_NUMBER n2;
        DWORD br2 = 0;
        ok = DeviceIoControl(hd, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,
                             &n2, sizeof(n2), &br2, NULL);
        CloseHandle(hd);
        if (!ok || (int)n2.DeviceNumber != disknum) continue;

        /* 3) Disk → USBSTOR → USB 两级父设备 */
        DEVINST usbstor, usb;
        if (CM_Get_Parent(&usbstor, devidata.DevInst, 0) != CR_SUCCESS) break;
        CM_Get_Device_IDW(usbstor, usbstor_id, 512, 0);
        if (CM_Get_Parent(&usb, usbstor, 0) == CR_SUCCESS) {
            wchar_t uid[512];
            if (CM_Get_Device_IDW(usb, uid, 512, 0) == CR_SUCCESS) {
                wchar_t* v = wcsstr(uid, L"VID_");
                wchar_t* p = wcsstr(uid, L"PID_");
                if (v && p) {
                    _snwprintf(vidpid, cchvp, L"VID:%.4hs PID:%.4hs", v + 4, p + 4);
                    found = TRUE;
                }
            }
        }
        if (!found) {
            /* 非 USB 通道（如读卡器）时 VID/PID 取不到，仍算成功以便显示序列号 */
            found = usbstor_id[0] != 0;
        }
    }
    SetupDiDestroyDeviceInfoList(hset);

    /* 4) 从 USBSTOR ID 提取序列号：USBSTOR\DISK&VEN_x&PROD_y\SERIAL&0 */
    if (found && usbstor_id[0]) {
        const wchar_t* sl = wcsrchr(usbstor_id, L'\\');
        if (sl) {
            sl++;
            wchar_t sn[64];
            lstrcpynW(sn, sl, 64);
            wchar_t* amp = wcschr(sn, L'&');   /* 去掉尾部实例号 &0 */
            if (amp) *amp = 0;
            lstrcpynW(usbserial, sn, cchsn);
        }
    }
    return found;
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
    dev_get_usbid(letter, out->vidpid, 64, out->usbserial, 64);
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
