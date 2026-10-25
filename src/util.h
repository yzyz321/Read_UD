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
wchar_t* utf8_to_wide(const char* s, int len);        /* 严格 UTF-8；失败返回 NULL */
wchar_t* ansi_to_wide(const char* s, int len);
int      wide_to_utf8(const wchar_t* s, char* out, int outlen);

/* ---------- 统一文本编码探测/解码 ---------- */
typedef enum {
    TEXT_ENC_UTF8 = 0,   /* UTF-8（无 BOM） */
    TEXT_ENC_UTF8_BOM,   /* UTF-8 带 BOM */
    TEXT_ENC_UTF16LE,    /* UTF-16LE（BOM 或零字节启发式） */
    TEXT_ENC_UTF16BE,    /* UTF-16BE（BOM 或零字节启发式） */
    TEXT_ENC_ANSI        /* ANSI/GBK（CP936） */
} TEXTENC;

/* 编码名（用于日志） */
const wchar_t* text_enc_name(TEXTENC enc);

/* 自动识别并严格解码文本。
 * 成功：TRUE，*out 为堆串（NUL 结尾；空输入返回空串），*enc=实际编码；
 * 失败：FALSE，*out=NULL，err（若提供）写入具体原因（含偏移/错误字节值）。
 * 识别顺序：BOM(UTF-8/UTF-16LE/BE) → 无 BOM 零字节启发式 UTF-16
 *           → 严格 UTF-8 → “少量非法字节的 UTF-8”明确拒绝（不静默 GBK）
 *           → GBK/CP936 回退；解码结果含内嵌 U+0000 按二进制拒绝。 */
BOOL text_decode(const void* data, size_t len, wchar_t** out, TEXTENC* enc,
                 wchar_t* err, size_t errlen);

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
/* 仅写文件日志（GUI 未就绪时使用）；文件按天缓存句柄、>8MB 轮转 .log.1，
 * 打开/写入失败时 OutputDebugStringW 兜底并尝试 logs\YYYYMMDD_alt.log */
void filelog(LOGLEVEL lv, const wchar_t* fmt, ...);
/* GUI 就绪后调用，启用控制台转发 */
void log_set_gui_hwnd(HWND hwnd);
/* 关闭日志文件缓存句柄（程序退出前调用；可重复调用安全） */
void log_shutdown(void);

/* ---------- GUI 消息（堆串所有权） ---------- */
/* 复制 text（NULL 视为空串）并 PostMessage 投递给 hwnd。
 * 成功返回 TRUE（接收方负责 free((void*)lparam)）；
 * 失败（hwnd 无效/复制或投递失败）返回 FALSE 且自行释放副本，不递归 applog。 */
BOOL post_owned_string(HWND hwnd, UINT msg, WPARAM wparam, const wchar_t* text);

/* ---------- 文件 ---------- */
/* 读文本文件并自动识别编码（UTF-8 ±BOM / UTF-16LE/BE ±BOM / GBK）。
 * 空文件成功返回空串；>64MB → FALSE + ERROR_FILE_TOO_LARGE；
 * 编码无法识别 → FALSE + ERROR_INVALID_DATA（原因写 filelog WARN）。 */
BOOL read_file_text(const wchar_t* path, wchar_t** out_text);
BOOL sha_or_md5_file(const wchar_t* path, wchar_t* md5hex_out, size_t outlen); /* MD5 十六进制 */

#endif
