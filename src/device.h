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

/* ---------- 设备身份签名（任务级身份绑定 / 格式化前后复核） ----------
 * 只保存"可用于区分个体"的最小信息：盘符 + 卷序列号 + USB VID/PID + USB 序列号。
 * 卷序列号在格式化后会变化，因此格式化的复核必须在 FormatEx 之前完成。 */
typedef struct {
    wchar_t drive;              /* 'D' ...（0=无效） */
    DWORD   volume_serial;      /* 卷序列号 */
    BOOL    has_serial;         /* 是否取到卷序列号 */
    wchar_t vidpid[64];         /* USB 硬件标识，如 "VID:0951 PID:1666" */
    wchar_t usbserial[64];      /* USB 序列号（同型号个体唯一） */
} DEVSIG;

/* 采集设备签名。失败返回 FALSE（不阻塞任务，调用方自行决定是否继续） */
BOOL dev_capture_sig(wchar_t drive, DEVSIG* out);

/* 判断两个签名是否仍指向同一设备：
 *   - 盘符必须相同；
 *   - 仅"双方都有"的身份信息参与比较（卷序列号 / VIDPID / USB 序列号）；
 *   - 双方都没有身份信息时返回 TRUE（信息不足，无法区分）。 */
BOOL dev_sig_same_device(const DEVSIG* a, const DEVSIG* b);

/* 生成可读描述："F: 卷序列号=1A2B3C4D VID:0951 PID:1666 SN:..." */
void dev_sig_describe(const DEVSIG* s, wchar_t* out, size_t outlen);

/* 枚举当前所有可移动磁盘，返回数量，mask 的 bit(n-'A') 置位。
 * infos 非 NULL 时只填卷信息（dev_get_info_volume，不跑 SetupAPI，避免轮询线程每盘取 USB ID） */
int dev_enum_removable(DWORD* mask, DEVINFO* infos /* 可为 NULL，至多 26 */);

/* 取单个盘的详细信息（失败返回 FALSE）：卷信息 + USB VID/PID/序列号 */
BOOL dev_get_info(wchar_t letter, DEVINFO* out);

/* 只取盘符根/卷标/文件系统/序列号/容量/可用空间，不调用 SetupAPI：
 * 快速，可安全在 UI 线程调用；vidpid/usbserial 留空；盘根不存在返回 FALSE。 */
BOOL dev_get_info_volume(wchar_t letter, DEVINFO* out);

/* 轮询线程：每 1s 枚举，盘符集合变化时向 hwnd 投递 WM_APP_DEVCOUNT。
 * 新事件协议：wparam = 本次变化的盘符位图（可多位），lparam = 1 插入 / 0 拔出；
 * 同一次轮询最多两条消息（先拔出位图、后插入位图），不再是最终全量 mask。 */
#define WM_APP_DEVCOUNT (WM_APP + 3)

/* 按 old_mask → new_mask 的差异投递 WM_APP_DEVCOUNT（可测试导出）：
 *   removed  = old & ~new → 一条 lparam=0（位图可多位）；
 *   inserted = new & ~old → 一条 lparam=1（位图可多位）；
 *   位图为空则不发。 */
void dev_post_changes(HWND hwnd, DWORD old_mask, DWORD new_mask);

BOOL dev_poll_start(HWND hwnd);
/* 停止轮询线程：SetEvent 唤醒 → 最多等待 5s Join。
 * 可重复调用；join 成功才关闭线程句柄，超时只写 filelog WARN 并保留句柄
 * （避免 use-after-close，下次调用可继续等待）。 */
void dev_poll_stop(void);

#endif
