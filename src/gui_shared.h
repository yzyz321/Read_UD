/* ============================================================
 * gui_shared.h - gui.c（主窗口）↔ gui_config.c（配置窗口）内部共享
 * 仅供本两个文件包含；对外接口见 gui.h。
 * ============================================================ */
#ifndef UDK_GUI_SHARED_H
#define UDK_GUI_SHARED_H

#include <windows.h>
#include "config.h"

/* ---------- gui.c 定义、gui_config.c 使用 ---------- */
extern HINSTANCE g_hinst;            /* 应用实例（控件创建用） */
extern HWND      g_hmain;            /* 主窗口句柄 */
extern HWND      g_hcfg;             /* 配置窗口句柄（dpi_update_font 刷新字体用） */
extern int       g_cur_model;        /* 主窗口当前机型索引 */
extern HFONT     g_font;             /* 主字体（配置窗口控件共用） */
int  dpi_scale(int v);               /* DPI 缩放（gui.c 定义） */
void dpi_update_font(void);          /* DPI 字体刷新（gui.c 定义） */
void refresh_buttons(void);          /* 主窗按钮状态刷新（gui.c 定义） */
void update_main_title(void);        /* 主窗标题刷新（gui.c 定义） */
BOOL reload_config_and_ui(void);     /* 重载 ini 并刷新主窗/配置窗（gui.c 定义） */

/* ---------- gui_config.c 定义、gui.c 使用 ---------- */
extern HWND g_hcfglist;              /* 配置窗口机型列表（reload 保存选择用） */
void cfg_register_class(HINSTANCE hInst);                  /* 注册配置窗口类 */
void open_config_dialog(void);                             /* 打开配置窗口 */
void cfg_rebuild_model_list(const wchar_t* keep_internal); /* 重载后刷新配置窗列表 */

#endif
