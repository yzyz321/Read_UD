/* ============================================================
 * util.h - 通用工具：宽字符串、编码转换、日志、文件辅助
 * ============================================================ */
#ifndef UDK_UTIL_H
#define UDK_UTIL_H

#include <windows.h>

/* ---------- 日志级别（决定控制台颜色前缀） ---------- */
typedef enum {
    LOG_INFO = 0,   /* 普通  灰白 */
    LOG_OK,         /* 成功  绿色 */
    LOG_WARN,       /* 警告  黄色 */
    LOG_ERR,        /* 错误  红色 */
    LOG_UI          /* 界面事件 蓝色 */
} LOGLEVEL;

/* 日志输出通道（对应 GUI 两个控制台区） */
#define CH_ENGINE 0     /* CMD-A 操作引擎输出（IDC_EDIT_ENGINE） */
#define CH_IDENT  1     /* CMD-B 标识生成输出（IDC_EDIT_IDENT）  */

/* ---------- 编码转换 ---------- */
wchar_t* utf8_to_wide(const char* s, int len);        /* 失败返回 NULL */
wchar_t* ansi_to_wide(const char* s, int len);
int      wide_to_utf8(const wchar_t* s, char* out, int outlen);

/* ---------- 字符串辅助 ---------- */
wchar_t* wcs_trim(wchar_t* s);
wchar_t* wcs_dup(const wchar_t* s);
/* 取文件所在目录（exe 目录等），返回堆缓冲 */
wchar_t* exe_dir(void);
/* 组合路径：dir + "\\" + name（dir 可为空） */
void path_join(wchar_t* out, size_t outlen, const wchar_t* dir, const wchar_t* name);

/* 递归创建目录链（已存在不报错）；返回最终目录是否存在 */
BOOL util_ensure_dirs(const wchar_t* path);

/* ---------- 日志 ---------- */
/* 任何线程均可调用；通过消息转发到 GUI 控制台并同步写文件日志（强制输出） */
void applog(int channel, LOGLEVEL lv, const wchar_t* fmt, ...);
/* 仅写文件日志（GUI 未就绪时使用） */
void filelog(LOGLEVEL lv, const wchar_t* fmt, ...);
/* GUI 就绪后调用，启用控制台转发 */
void log_set_gui_hwnd(HWND hwnd);

/* ---------- 文件 ---------- */
BOOL read_file_text(const wchar_t* path, wchar_t** out_text); /* 自动识别 UTF-8/GBK，返回堆串 */
BOOL sha_or_md5_file(const wchar_t* path, wchar_t* md5hex_out, size_t outlen); /* MD5 十六进制 */

#endif
