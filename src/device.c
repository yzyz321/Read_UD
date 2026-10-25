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
#include "util.h"   /* filelog：轮询线程启动/停止失败时的诊断日志（退出期不能用 applog） */

/* GUID_DEVINTERFACE_DISK {53F56307-B6BF-11D0-94F2-00A0C91EFB8B} */
DEFINE_GUID(UDK_GUID_DISK, 0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2,
            0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);

static HANDLE g_thread = NULL;
static HANDLE g_stop_evt = NULL;    /* 轮询停止事件：dev_poll_start 创建、join 成功后关闭 */
static volatile BOOL g_run = FALSE;
static HWND g_hwnd = NULL;

/* 仅卷信息 helper（不调用 SetupAPI；见 dev_volume_info 定义） */
static BOOL dev_volume_info(wchar_t letter, DEVINFO* out);

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
                dev_get_info_volume(d, &infos[count]);   /* 轮询线程不跑 SetupAPI */
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

        /* 3) 磁盘父设备链：实测 Windows 上为
         *      USBSTOR\DISK&VEN_x&PROD_y\SERIAL&0  →  USB\VID_xxxx&PID_yyyy\SERIAL  →  Hub
         *    VID/PID 必须取“第一个带 VID_/PID_ 的父设备”（即 USB 设备本身）。
         *    （旧实现固定取第二级父设备，会把上级 Hub 的 VID/PID 当成设备标识：
         *      实测 F: 得到 05E3/0610 而不是 058F/6387，导致 USB Hub 端口匹配失败。） */
        DEVINST usbstor, usb;
        if (CM_Get_Parent(&usbstor, devidata.DevInst, 0) != CR_SUCCESS) break;
        CM_Get_Device_IDW(usbstor, usbstor_id, 512, 0);
        {
            wchar_t* v = wcsstr(usbstor_id, L"VID_");
            wchar_t* p = wcsstr(usbstor_id, L"PID_");
            if (v && p) {
                /* v/p 指向宽字符设备 ID（L"USB\\VID_058F&PID_6387\\..."），
                 * 必须用 %ls（宽串）取 4 个字符；旧代码用 %hs（窄串）会得到
                 * "VID:0 PID:0"，导致 usb_query_speed 无法匹配 Hub 端口。 */
                _snwprintf(vidpid, cchvp, L"VID:%.4ls PID:%.4ls", v + 4, p + 4);
                found = TRUE;
            }
        }
        if (!found && CM_Get_Parent(&usb, usbstor, 0) == CR_SUCCESS) {
            /* 少数链路多一级 USBSTOR 中间节点：再上溯一级找 USB 设备 ID */
            wchar_t uid[512];
            if (CM_Get_Device_IDW(usb, uid, 512, 0) == CR_SUCCESS) {
                wchar_t* v = wcsstr(uid, L"VID_");
                wchar_t* p = wcsstr(uid, L"PID_");
                if (v && p) {
                    _snwprintf(vidpid, cchvp, L"VID:%.4ls PID:%.4ls", v + 4, p + 4);
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

/* 内部：盘符根 + 卷信息（卷标/文件系统/序列号/容量/可用空间），不调用 SetupAPI。
 * 盘根不存在（DRIVE_NO_ROOT_DIR/DRIVE_UNKNOWN）返回 FALSE；
 * RAW/未格式化盘根存在但 GetVolumeInformation 失败时仍返回 TRUE（has_serial=FALSE），
 * 保持 dev_capture_sig 对 RAW 盘“盘根存在即可采集”的既有行为。 */
static BOOL dev_volume_info(wchar_t letter, DEVINFO* out)
{
    UINT dt;
    DWORD volser = 0, maxcomp = 0, fsflags = 0;
    ULARGE_INTEGER total, freeq;

    if (!out) return FALSE;
    memset(out, 0, sizeof(DEVINFO));
    out->letter = letter;
    _snwprintf(out->root, 8, L"%c:\\", letter);
    out->root[7] = 0;

    dt = GetDriveTypeW(out->root);
    if (dt == DRIVE_NO_ROOT_DIR || dt == DRIVE_UNKNOWN) return FALSE;

    if (GetVolumeInformationW(out->root, out->volume, 128,
                              &volser, &maxcomp, &fsflags,
                              out->fs, 32)) {
        out->serial = volser;
        out->has_serial = TRUE;
    } else {
        out->volume[0] = 0;
        lstrcpynW(out->fs, L"未知", 32);
    }

    if (GetDiskFreeSpaceExW(out->root, NULL, &total, &freeq)) {
        out->total_bytes = total.QuadPart;
        out->free_bytes = freeq.QuadPart;
    }
    return TRUE;
}

/* UI 线程可用：只取卷信息（不跑 SetupAPI），vidpid/usbserial 留空 */
BOOL dev_get_info_volume(wchar_t letter, DEVINFO* out)
{
    return dev_volume_info(letter, out);
}

BOOL dev_get_info(wchar_t letter, DEVINFO* out)
{
    if (!dev_volume_info(letter, out)) return FALSE;
    dev_get_usbid(letter, out->vidpid, 64, out->usbserial, 64);
    return TRUE;
}

/* ---------- 设备身份签名 ---------- */

BOOL dev_capture_sig(wchar_t drive, DEVSIG* out)
{
    if (!out) return FALSE;
    memset(out, 0, sizeof(*out));
    if (!drive) return FALSE;
    out->drive = drive;

    /* 盘符根不存在（设备已拔出/盘符失效）→ 采集失败，调用方据此中止破坏性动作 */
    wchar_t root[8];
    _snwprintf(root, 8, L"%c:\\", drive);
    UINT dt = GetDriveTypeW(root);
    if (dt == DRIVE_NO_ROOT_DIR || dt == DRIVE_UNKNOWN) return FALSE;

    DEVINFO di;
    if (!dev_get_info(drive, &di)) return FALSE;
    if (di.has_serial) {
        out->volume_serial = di.serial;
        out->has_serial = TRUE;
    }
    lstrcpynW(out->vidpid, di.vidpid, 64);
    lstrcpynW(out->usbserial, di.usbserial, 64);
    return TRUE;
}

BOOL dev_sig_same_device(const DEVSIG* a, const DEVSIG* b)
{
    if (!a || !b) return FALSE;
    if (!a->drive || !b->drive) return FALSE;
    if (a->drive != b->drive) return FALSE;            /* 盘符变化=不同目标 */

    /* 仅"双方都有"的身份信息参与比较，信息缺失时不做否定判断 */
    if (a->has_serial && b->has_serial && a->volume_serial != b->volume_serial)
        return FALSE;
    if (a->vidpid[0] && b->vidpid[0] && _wcsicmp(a->vidpid, b->vidpid) != 0)
        return FALSE;
    if (a->usbserial[0] && b->usbserial[0] &&
        _wcsicmp(a->usbserial, b->usbserial) != 0)
        return FALSE;
    return TRUE;
}

void dev_sig_describe(const DEVSIG* s, wchar_t* out, size_t outlen)
{
    if (!out || outlen == 0) return;
    out[0] = 0;
    if (!s) return;
    wchar_t serial[32];
    if (s->has_serial)
        _snwprintf(serial, 32, L"%08lX", (unsigned long)s->volume_serial);
    else
        lstrcpynW(serial, L"未知", 32);
    /* vidpid 本身通常已带 "VID:" 前缀（如 "VID:0951 PID:1666"），避免重复拼接 */
    wchar_t viddisp[80];
    if (!s->vidpid[0])
        lstrcpynW(viddisp, L"VID:未知", 80);
    else if (_wcsnicmp(s->vidpid, L"VID:", 4) == 0)
        lstrcpynW(viddisp, s->vidpid, 80);
    else
        _snwprintf(viddisp, 80, L"VID:%s", s->vidpid);
    viddisp[79] = 0;
    wchar_t sndisp[80];
    if (s->usbserial[0]) lstrcpynW(sndisp, s->usbserial, 80);
    else lstrcpynW(sndisp, L"未知", 80);

    _snwprintf(out, outlen, L"%c: 卷序列号=%s %s SN:%s",
               s->drive ? s->drive : L'?', serial, viddisp, sndisp);
    out[outlen - 1] = 0;
}

/* ---------- 轮询线程 ---------- */

/* 按 old_mask → new_mask 的差异投递设备变化消息（同一次轮询最多两条：先拔出、后插入）。
 * 位图语义见 device.h：wparam=变化盘符位图，lparam=1 插入 / 0 拔出；空位图不发。 */
void dev_post_changes(HWND hwnd, DWORD old_mask, DWORD new_mask)
{
    if (!hwnd) return;
    DWORD removed = old_mask & ~new_mask;
    DWORD inserted = new_mask & ~old_mask;
    if (removed)
        PostMessageW(hwnd, WM_APP_DEVCOUNT, (WPARAM)removed, (LPARAM)0);
    if (inserted)
        PostMessageW(hwnd, WM_APP_DEVCOUNT, (WPARAM)inserted, (LPARAM)1);
}

static DWORD WINAPI poll_thread(LPVOID param)
{
    (void)param;
    DWORD prev_mask = 0;
    BOOL first = TRUE;

    while (g_run) {
        DWORD mask = 0;
        dev_enum_removable(&mask, NULL);

        if (!first && mask != prev_mask)
            dev_post_changes(g_hwnd, prev_mask, mask);   /* 只报差异位图，不再报全量 mask */
        prev_mask = mask;
        first = FALSE;

        if (!g_run) break;

        /* 等待停止事件（1000ms 超时 = 保持 1s 轮询语义；dev_poll_stop 可立即唤醒） */
        {
            DWORD w = WaitForSingleObject(g_stop_evt, 1000);
            if (w == WAIT_OBJECT_0) break;              /* 停止事件已置位 */
            if (w == WAIT_FAILED) {
                /* 不能用 applog（退出期窗口可能已销毁），只写 filelog */
                filelog(LOG_WARN, L"[DEV] 轮询等待停止事件失败（错误码 %lu），退出轮询线程",
                        (unsigned long)GetLastError());
                break;
            }
            /* WAIT_TIMEOUT：继续下一轮 */
        }
    }
    return 0;
}

BOOL dev_poll_start(HWND hwnd)
{
    if (g_thread) return TRUE;                      /* 已在运行 */
    if (!g_stop_evt) {
        g_stop_evt = CreateEventW(NULL, TRUE, FALSE, NULL);   /* 手动复位停止事件 */
        if (!g_stop_evt) {
            filelog(LOG_WARN, L"[DEV] 创建停止事件失败（错误码 %lu），轮询线程未启动",
                    (unsigned long)GetLastError());
            return FALSE;
        }
    }
    ResetEvent(g_stop_evt);
    g_hwnd = hwnd;
    g_run = TRUE;
    g_thread = CreateThread(NULL, 0, poll_thread, NULL, 0, NULL);
    if (!g_thread) {
        DWORD e = GetLastError();
        g_run = FALSE;
        CloseHandle(g_stop_evt);
        g_stop_evt = NULL;
        filelog(LOG_WARN, L"[DEV] 创建轮询线程失败（错误码 %lu）", (unsigned long)e);
        return FALSE;
    }
    return TRUE;
}

void dev_poll_stop(void)
{
    if (!g_thread) {                                /* 重复调用安全 */
        if (g_stop_evt) { CloseHandle(g_stop_evt); g_stop_evt = NULL; }
        return;
    }
    g_run = FALSE;
    if (g_stop_evt)
        SetEvent(g_stop_evt);                       /* 立即唤醒 WaitForSingleObject */
    else
        filelog(LOG_WARN, L"[DEV] dev_poll_stop：停止事件句柄缺失，仅置退出标志并等待");

    {
        DWORD w = WaitForSingleObject(g_thread, 5000);
        if (w == WAIT_OBJECT_0) {
            /* 线程已退出，才能安全关闭句柄 */
            CloseHandle(g_thread);
            g_thread = NULL;
            if (g_stop_evt) { CloseHandle(g_stop_evt); g_stop_evt = NULL; }
        } else {
            /* 5s 未退出：线程仍在运行，绝不能 CloseHandle(g_thread)（避免 use-after-close）。
             * 保留线程/事件句柄，重复调用 dev_poll_stop 会再次 SetEvent + 等待。
             * 只写 filelog，不 applog（退出期窗口可能已销毁）。 */
            filelog(LOG_WARN,
                    L"[DEV] 轮询线程 5s 内未退出（等待结果=%lu），保留线程/事件句柄，下次 stop 再试",
                    (unsigned long)w);
        }
    }
}
