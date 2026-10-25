/* ============================================================
 * ops.h - 操作引擎：write / read / format / usbinfo / 标签 / rwtest
 * 全部函数在工作线程中调用，通过 applog 输出到 CMD 区 + 文件日志
 *
 * ---------- 统一结果码（所有 op_* 函数返回） ----------
 * 判定政策（用户明确要求）：
 *   “只要跳过是按照配置文件规定正常屏蔽的操作步骤，判断结果仍按成功对待；
 *     当跳过是程序为了运行完整或不崩溃而自行进行的跳过，不在此列。”
 * 因此：
 *   - 配置正常屏蔽（ENABLE_*=0、READFILE 留空、READRULE 留空、
 *     AUTO_COPY/AUTO_TEST=0 的自动裁剪）→ OP_SKIP_CFG：
 *     按成功对待（不改变任务成功结论），但要单独计数/展示；
 *   - 程序自行降级/未实现/无法校验（PROCESSED、REGEX、未识别 READRULE、
 *     HASH 非 md5、MD5 算不出、读取中换盘等）→ OP_FAIL：
 *     不得静默跳过，不得报成功；
 *   - 操作员停止 → OP_STOPPED。
 * ============================================================ */
#ifndef UDK_OPS_H
#define UDK_OPS_H

#include <windows.h>
#include "config.h"
#include "device.h"

/* ---------- 统一结果码（供 ops/gui 共用；数值 0/1/2 与旧版兼容） ---------- */
#define OP_OK        0  /* 成功（含“按配置正常屏蔽”的步骤不改变任务成功结论） */
#define OP_FAIL      1  /* 失败；也用于“程序自行降级/跳过/未实现/无法校验”，不得报成功 */
#define OP_STOPPED   2  /* 操作员停止 */
#define OP_SKIP_CFG  3  /* 按配置正常屏蔽（ENABLE_*=0、READFILE 留空、READRULE 留空、
                         * AUTO_COPY/AUTO_TEST=0），按成功对待但单独计数/展示 */

/* 全局停止标志（[停止] 按钮置位）。请勿直接写，统一用 ops_request_stop()/ops_reset_stop() */
extern volatile LONG g_stop;
#define CHECK_STOP() (g_stop != 0)

/* 请求停止：置 g_stop 并取消正在进行的 CopyFileEx 复制 */
void ops_request_stop(void);
/* 任务开始前清零停止状态（含复制取消标志） */
void ops_reset_stop(void);

/* 任务触发方式（用于 GUI 模式标记） */
#define TRIGGER_AUTO   1   /* 插盘自动 */
#define TRIGGER_MANUAL 2   /* 按钮/热键 */

/* ---------- 各操作（返回 OP_* 结果码） ---------- */
/* expect = 任务启动时采集的设备身份基线（可为 NULL：函数内部自行采集） */
int op_write(const CFGMODEL* m, wchar_t drive, const DEVSIG* expect);   /* 写入（COPY 模式） */
int op_read(const CFGMODEL* m, wchar_t drive);             /* 读取+判定，结果显示 */
int op_format(const CFGMODEL* m, wchar_t drive, HWND hwnd, const DEVSIG* expect); /* 格式化（强制确认+身份复核） */
int op_usbinfo(const CFGMODEL* m, wchar_t drive);          /* USB 通讯模式识别 */
int op_label_read(const CFGMODEL* m, wchar_t drive);       /* 读取标签 */
int op_rwtest(const CFGMODEL* m, wchar_t drive, const DEVSIG* expect); /* 读写测试（写读比对） */

/* 标识生成输出（CMD-B 通道演示：SN/MAC 按台生成，Phase 3 完整化） */
int op_gen_ident(const CFGMODEL* m, wchar_t drive, int index);

/* 读取结果传递给 GUI（判定 + 内容）——通过 WM_APP_VERDICT/FILECONTENT 消息 */
void ops_set_result_target(HWND hwnd);

#endif
