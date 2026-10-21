/* ============================================================
 * ops.h - 操作引擎：write / read / format / usbinfo / 标签 / rwtest
 * 全部函数在工作线程中调用，通过 applog 输出到 CMD 区 + 文件日志
 * 返回 0=成功，非 0=失败
 * ============================================================ */
#ifndef UDK_OPS_H
#define UDK_OPS_H

#include <windows.h>
#include "config.h"

/* 全局停止标志（[停止] 按钮置位） */
extern volatile LONG g_stop;
#define CHECK_STOP() (g_stop != 0)

/* 任务触发方式（用于 GUI 模式标记） */
#define TRIGGER_AUTO   1   /* 插盘自动 */
#define TRIGGER_MANUAL 2   /* 按钮/热键 */

/* ---------- 各操作 ---------- */
int op_write(const CFGMODEL* m, wchar_t drive);            /* 写入（COPY 模式） */
int op_read(const CFGMODEL* m, wchar_t drive);             /* 读取+判定，结果显示 */
int op_format(const CFGMODEL* m, wchar_t drive, HWND hwnd);/* 格式化（含确认） */
int op_usbinfo(const CFGMODEL* m, wchar_t drive);          /* USB 通讯模式识别 */
int op_label_read(const CFGMODEL* m, wchar_t drive);       /* 读取标签 */
int op_label_write(const CFGMODEL* m, wchar_t drive);      /* 写入标签（回读校验） */
int op_rwtest(const CFGMODEL* m, wchar_t drive);           /* 读写测试（写读比对） */

/* 标识生成输出（CMD-B 通道演示：SN/MAC 按台生成，Phase 3 完整化） */
int op_gen_ident(const CFGMODEL* m, wchar_t drive, int index);

/* 读取结果传递给 GUI（判定 + 内容）——通过 WM_APP_VERDICT/FILECONTENT 消息 */
void ops_set_result_target(HWND hwnd);

#endif
