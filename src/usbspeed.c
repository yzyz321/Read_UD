/* ============================================================
 * usbspeed.c - USB 通讯模式（链路速度）识别
 *
 * 原理：USB 设备实际协商出来的链路速度，只能从"它挂在哪个 Hub 的哪个
 * 端口"获知（设备本身没有可查询的速度属性）。因此：
 *   1) SetupAPI 枚举所有 USB Hub 设备接口（GUID_DEVINTERFACE_USB_HUB）；
 *   2) 打开 Hub，用 IOCTL_USB_GET_NODE_INFORMATION 取端口数；
 *   3) 逐端口用 IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX 读取
 *      DeviceDescriptor，与 DEVINFO->vidpid 解析出的 VID/PID 匹配；
 *   4) 用 EX.Speed 映射速度（0=Low 1=Full 2=High 3=Super）；SuperSpeed
 *      再尝试 _EX_V2 区分 5Gbps（USB 3.0/3.1 Gen1）与 10Gbps（Gen2）。
 *
 * 说明：旧实现用 IOCTL_STORAGE_QUERY_PROPERTY + 自造 PROTO_DATA 查询协议
 * 规格（属性 ID/协议类型/结构布局均有误），本文件完全替代该做法。
 * ============================================================ */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>      /* CTL_CODE/METHOD_BUFFERED（WIN32_LEAN_AND_MEAN 下不自动包含） */
#include <setupapi.h>
#include <usbioctl.h>      /* IOCTL_USB_GET_NODE_* / USB_NODE_*（间接包含 usbspec.h） */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "usbspeed.h"

/* GUID_DEVINTERFACE_USB_HUB = {F18A0E88-C30C-11D0-8815-00A0C906BED8}
 * 自行定义静态 GUID，避免依赖 usbiodef.h 的 DEFINE_GUID/INITGUID 链接细节。 */
static const GUID k_usb_hub_guid =
    { 0xf18a0e88, 0xc30c, 0x11d0, { 0x88, 0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8 } };

#define DETAIL_CCH (USBSPEED_DETAIL_LEN)

/* ---------------- 细节文本（保证 NUL） ---------------- */
static void set_detail(USBSpeed* out, const wchar_t* fmt, ...)
{
    if (!out) return;
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(out->detail, DETAIL_CCH - 1, fmt, ap);
    va_end(ap);
    out->detail[DETAIL_CCH - 1] = 0;
}

const wchar_t* usb_speed_name(int speed)
{
    switch (speed) {
    case 1: return L"USB Low-Speed (1.5Mbps)";
    case 2: return L"USB Full-Speed (12Mbps)";
    case 3: return L"USB 2.0 High-Speed (480Mbps)";
    case 4: return L"USB 3.0 SuperSpeed (5Gbps)";
    case 5: return L"USB 3.1 SuperSpeed+ (10Gbps)";
    default: return NULL;
    }
}

/* ---------------- VID/PID 解析 ---------------- */

/* 严格 4 位十六进制（device.c 生成 "VID:0951 PID:1666"；兼容大小写） */
static BOOL parse_hex4(const wchar_t* s, unsigned* out)
{
    if (!s || !out) return FALSE;
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        wchar_t c = s[i];
        unsigned d;
        if (c >= L'0' && c <= L'9')      d = (unsigned)(c - L'0');
        else if (c >= L'a' && c <= L'f') d = (unsigned)(c - L'a' + 10);
        else if (c >= L'A' && c <= L'F') d = (unsigned)(c - L'A' + 10);
        else return FALSE;
        v = (v << 4) | d;
    }
    *out = v;
    return TRUE;
}

static BOOL parse_vidpid(const wchar_t* vidpid, unsigned* vid, unsigned* pid)
{
    if (!vidpid || !vidpid[0] || !vid || !pid) return FALSE;
    const wchar_t* v = wcsstr(vidpid, L"VID:");
    const wchar_t* p = wcsstr(vidpid, L"PID:");
    if (!v || !p) return FALSE;
    return parse_hex4(v + 4, vid) && parse_hex4(p + 4, pid);
}

/* ---------------- Hub 打开（先 GENERIC_WRITE，失败再用 0 重试） ---------------- */
static HANDLE open_hub(const wchar_t* path, DWORD* first_err)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) return h;

    DWORD e1 = GetLastError();
    /* Hub IOCTL 均带 FILE_ANY_ACCESS，部分系统/权限下 GENERIC_WRITE 会被拒绝，
     * 用 dwDesiredAccess=0 再尝试一次（只做 IOCTL 不需要读写权限）。 */
    h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) return h;

    if (first_err) *first_err = e1 ? e1 : GetLastError();
    return INVALID_HANDLE_VALUE;
}

/* ---------------- 端口速度判定 ---------------- */
/* 返回 1..5；0 = 端口报告的速度值无法识别 */
static int query_port_speed(HANDLE hub, ULONG port_index, UCHAR ex_speed)
{
    switch (ex_speed) {
    case UsbLowSpeed:   return 1;
    case UsbFullSpeed:  return 2;
    case UsbHighSpeed:  return 3;
    case UsbSuperSpeed: {
        /* SuperSpeed 还要区分 5G（USB 3.0/3.1 Gen1）与 10G（Gen2）：_EX_V2 查询 */
        USB_NODE_CONNECTION_INFORMATION_EX_V2 v2;
        memset(&v2, 0, sizeof(v2));
        v2.ConnectionIndex = port_index;
        v2.Length = (ULONG)sizeof(v2);
        DWORD bytes = 0;
        if (DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2,
                            &v2, (DWORD)sizeof(v2), &v2, (DWORD)sizeof(v2),
                            &bytes, NULL)) {
            if (v2.Flags.DeviceIsOperatingAtSuperSpeedPlusOrHigher) return 5;
        }
        return 4;   /* _EX_V2 不可用/失败：按 5Gbps 处理 */
    }
    default:
        return 0;
    }
}

/* ---------------- 主入口 ---------------- */
BOOL usb_query_speed(const DEVINFO* di, USBSpeed* out)
{
    if (out) {
        out->speed = 0;
        out->detail[0] = 0;
    }
    if (!di || !out) {
        set_detail(out, L"参数无效（DEVINFO/USBSpeed 为空）");
        return FALSE;
    }

    unsigned vid = 0, pid = 0;
    if (!di->vidpid[0]) {
        set_detail(out, L"DEVINFO 无 USB 硬件标识（vidpid 为空，可能不是 USB 设备）");
        return FALSE;
    }
    if (!parse_vidpid(di->vidpid, &vid, &pid)) {
        set_detail(out, L"VID/PID 解析失败（vidpid=\"%s\"，应形如 \"VID:xxxx PID:yyyy\"）",
                   di->vidpid);
        return FALSE;
    }

    HDEVINFO set = SetupDiGetClassDevsW(&k_usb_hub_guid, NULL, NULL,
                                        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) {
        set_detail(out, L"无法枚举 USB 集线器（SetupAPI 错误码 %lu）", GetLastError());
        return FALSE;
    }

    int hub_count = 0;         /* SetupAPI 枚举到的 Hub 接口数 */
    int hub_opened = 0;        /* 成功打开的 Hub 数 */
    int port_total = 0;        /* 已检查的端口总数 */
    int match_count = 0;       /* VID/PID 命中数 */
    int match_speed = 0;       /* 唯一命中时的速度（0=未知） */
    UCHAR match_raw_speed = 0; /* 唯一命中时端口报告的速度原值（诊断用） */
    DWORD first_open_err = 0;  /* 第一个打开失败的 Hub 错误码 */

    SP_DEVICE_INTERFACE_DATA ifd;
    memset(&ifd, 0, sizeof(ifd));

    for (DWORD idx = 0; ; idx++) {
        ifd.cbSize = sizeof(ifd);
        if (!SetupDiEnumDeviceInterfaces(set, NULL, &k_usb_hub_guid, idx, &ifd))
            break;
        hub_count++;

        /* 先问所需缓冲大小（UNICODE 下 SP_DEVICE_INTERFACE_DETAIL_DATA_W） */
        DWORD need = 0;
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, NULL, 0, &need, NULL)) {
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || need == 0)
                continue;
        }
        if (need > 64 * 1024) continue;

        SP_DEVICE_INTERFACE_DETAIL_DATA_W* det =
            (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(need);
        if (!det) continue;
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        BOOL got = SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, NULL, NULL);
        if (!got) { free(det); continue; }

        DWORD open_err = 0;
        HANDLE hub = open_hub(det->DevicePath, &open_err);
        free(det);
        if (hub == INVALID_HANDLE_VALUE) {
            if (!first_open_err) first_open_err = open_err;
            continue;
        }
        hub_opened++;

        USB_NODE_INFORMATION ni;
        memset(&ni, 0, sizeof(ni));
        DWORD bytes = 0;
        if (DeviceIoControl(hub, IOCTL_USB_GET_NODE_INFORMATION, NULL, 0,
                            &ni, (DWORD)sizeof(ni), &bytes, NULL)) {
            int ports = (int)ni.u.HubInformation.HubDescriptor.bNumberOfPorts;
            if (ports > 0) port_total += ports;
            for (int port = 1; port <= ports; port++) {
                USB_NODE_CONNECTION_INFORMATION_EX ex;
                memset(&ex, 0, sizeof(ex));
                ex.ConnectionIndex = (ULONG)port;
                if (!DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
                                     &ex, (DWORD)sizeof(ex), &ex, (DWORD)sizeof(ex),
                                     &bytes, NULL))
                    continue;
                if (ex.ConnectionStatus != DeviceConnected) continue;
                if (ex.DeviceDescriptor.idVendor != (USHORT)vid ||
                    ex.DeviceDescriptor.idProduct != (USHORT)pid)
                    continue;

                match_count++;
                if (match_count == 1) {
                    match_raw_speed = ex.Speed;
                    match_speed = query_port_speed(hub, (ULONG)port, ex.Speed);
                }
            }
        }
        CloseHandle(hub);
    }
    SetupDiDestroyDeviceInfoList(set);

    if (hub_count == 0) {
        set_detail(out, L"未枚举到 USB 集线器（系统未返回 Hub 设备接口）");
        return FALSE;
    }
    if (hub_opened == 0) {
        set_detail(out, L"无法打开 USB 集线器（错误码 %lu）",
                   first_open_err ? first_open_err : ERROR_ACCESS_DENIED);
        return FALSE;
    }
    if (match_count == 0) {
        set_detail(out, L"未找到 VID:%04X PID:%04X 匹配的 USB 端口（已检查 %d 个 hub / %d 个端口）",
                   vid, pid, hub_opened, port_total);
        return FALSE;
    }
    if (match_count > 1) {
        set_detail(out, L"多个同 VID:%04X PID:%04X 设备（命中 %d 个），无法唯一匹配",
                   vid, pid, match_count);
        return FALSE;
    }
    if (match_speed == 0) {
        set_detail(out, L"已匹配 VID:%04X PID:%04X 的 USB 端口，但端口报告的速度值未知（%u）",
                   vid, pid, (unsigned)match_raw_speed);
        return FALSE;
    }

    out->speed = match_speed;
    set_detail(out, L"USB Hub 端口匹配（VID:%04X PID:%04X）", vid, pid);
    return TRUE;
}
