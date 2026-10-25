/* ============================================================
 * usbspeed.h - USB 通讯模式（链路速度）识别
 *
 * 速度取值（USBSpeed.speed）：
 *   1 = Low-Speed    (1.5 Mbps)
 *   2 = Full-Speed   (12 Mbps)
 *   3 = High-Speed   (480 Mbps)
 *   4 = SuperSpeed   (5 Gbps)
 *   5 = SuperSpeed+  (10 Gbps)
 * detail：给日志/现场判断用的原因文本（成功时为匹配到的端口信息，
 *         失败时为未找到/多命中/无权限等具体原因），始终 NUL 结尾。
 * ============================================================ */
#ifndef UDK_USBSPEED_H
#define UDK_USBSPEED_H

#include <windows.h>
#include "device.h"

#define USBSPEED_DETAIL_LEN 160

typedef struct {
    int     speed;                              /* 1..5；失败/未知时为 0 */
    wchar_t detail[USBSPEED_DETAIL_LEN];        /* 原因/匹配信息（NUL 保证） */
} USBSpeed;

/* 通过 USB Hub IOCTL 匹配 DEVINFO->vidpid（"VID:xxxx PID:yyyy"）识别链路速度。
 * TRUE  = 找到且唯一匹配，out->speed 为 1..5，out->detail 非空；
 * FALSE = 未找到/无法确定/多命中/无权限，out->detail 写原因（不崩溃、不阻塞）。
 * di 或 out 为 NULL 时返回 FALSE。 */
BOOL usb_query_speed(const DEVINFO* di, USBSpeed* out);

/* 速度编号 → 可读名称；未知返回 NULL */
const wchar_t* usb_speed_name(int speed);

#endif
