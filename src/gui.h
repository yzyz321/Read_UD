/* ============================================================
 * gui.h - GUI 消息与运行任务接口
 * ============================================================ */
#ifndef UDK_GUI_H
#define UDK_GUI_H

#include <windows.h>
#include "config.h"

/* ---------- GUI 自定义消息（ops/device 线程 → 主窗口） ---------- */
#define WM_APP_LOG        (WM_APP + 1)   /* lparam=堆串(自释放) wparam=ch|(lv<<16) */
#define WM_APP_PROGRESS   (WM_APP + 2)   /* wparam=0..100 */
#define WM_APP_DEVCOUNT   (WM_APP + 3)   /* wparam=mask lparam=1插入/0拔出 */
#define WM_APP_VERDICT    (WM_APP + 4)   /* lparam=L"PASS"/L"FAIL"（堆串） */
#define WM_APP_FILECONTENT (WM_APP + 5)  /* lparam=堆串 内容 */
#define WM_APP_LABEL      (WM_APP + 6)   /* lparam=堆串 标签文本 */
#define WM_APP_ERROR      (WM_APP + 7)   /* wparam=1亮出错徽标 0熄灭 */
#define WM_APP_TASKDONE   (WM_APP + 8)   /* wparam=退出码 */
#define WM_APP_MODE       (WM_APP + 9)   /* wparam=TRIGGER_AUTO/MANUAL */

/* ---------- 运行状态 ---------- */
extern volatile LONG g_running;      /* 任务执行中 */
extern CONFIG g_cfg;                 /* 全局配置 */
extern wchar_t g_ini_path[MAX_PATH]; /* ini 路径 */

/* 在工作线程执行完整任务序列（自动/手动），结束后投递 WM_APP_TASKDONE */
void task_run_async(int model_index, int trigger);

/* GUI 入口 */
int gui_run(HINSTANCE hInst, int nShow);

#endif
