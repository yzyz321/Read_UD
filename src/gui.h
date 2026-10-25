/* ============================================================
 * gui.h - GUI 消息与运行任务接口
 * ============================================================ */
#ifndef UDK_GUI_H
#define UDK_GUI_H

#include <windows.h>
#include "config.h"

/* 主窗口类名（main.c 单实例 FindWindow 与 gui_run 注册/创建共用同一宏，P3-6） */
#define UDK_MAIN_WNDCLASS L"UDK_MAINCLASS"

/* ---------- GUI 自定义消息（ops/device 线程 → 主窗口） ---------- */
#define WM_APP_LOG        (WM_APP + 1)   /* lparam=堆串(自释放) wparam=ch|(lv<<16) */
#define WM_APP_PROGRESS   (WM_APP + 2)   /* wparam=0..100 */
#define WM_APP_DEVCOUNT   (WM_APP + 3)   /* wparam=本次变化盘符位图（可多位） lparam=1插入/0拔出 */
#define WM_APP_VERDICT    (WM_APP + 4)   /* lparam=L"PASS"/L"FAIL"（堆串） */
#define WM_APP_FILECONTENT (WM_APP + 5)  /* lparam=堆串 内容 */
#define WM_APP_LABEL      (WM_APP + 6)   /* lparam=堆串 标签文本 */
#define WM_APP_ERROR      (WM_APP + 7)   /* wparam=1亮出错徽标 0熄灭 */
#define WM_APP_TASKDONE   (WM_APP + 8)   /* wparam=退出码 */
#define WM_APP_MODE       (WM_APP + 9)   /* wparam=TRIGGER_AUTO/MANUAL */
#define WM_APP_MONINFO    (WM_APP + 10)  /* lparam=堆串 "标题\n副行"（监控状态） */
#define WM_APP_DEVREFRESH (WM_APP + 11)  /* 格式化等操作完成后：请求重新采集并刷新设备/标签行 */

/* ---------- 运行状态 ---------- */
extern volatile LONG g_running;      /* 任务执行中 */
extern CONFIG g_cfg;                 /* 全局配置 */
extern wchar_t g_ini_path[MAX_PATH]; /* ini 路径 */

/* 在工作线程执行完整任务序列（自动/手动），结束后投递 WM_APP_TASKDONE
 * drive = 目标盘符（如 'F'）；0 表示沿用"取第一个可移动盘符"的旧逻辑 */
void task_run_async(int model_index, int trigger, wchar_t drive);

/* GUI 入口 */
int gui_run(HINSTANCE hInst, int nShow);

#endif
