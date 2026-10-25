/* ============================================================
 * config.h - ini 配置解析（UdiskTools.ini）
 * 结构：[TOOLSINFO] 全局节 + 若干机型节（节名 = PRJNAMEn 内部键）
 * ============================================================ */
#ifndef UDK_CONFIG_H
#define UDK_CONFIG_H

#include <windows.h>

#define CFG_MAX_MODELS  16
#define CFG_MAX_KEYS    48
#define CFG_KEY_LEN     64
#define CFG_VAL_LEN     512

typedef struct {
    wchar_t key[CFG_KEY_LEN];
    wchar_t val[CFG_VAL_LEN];
} CFGKEY;

typedef struct {
    wchar_t internal[CFG_KEY_LEN];   /* 内部键 = 节名（程序依赖，永不改动） */
    wchar_t display[CFG_VAL_LEN];    /* 显示名 DISPLAYNAMEn（界面显示）      */
    CFGKEY   keys[CFG_MAX_KEYS];
    int      nkeys;
} CFGMODEL;

typedef struct {
    wchar_t  title[CFG_VAL_LEN];      /* TITLE   */
    wchar_t  version[CFG_VAL_LEN];    /* VERSION */
    wchar_t  adminpass[CFG_VAL_LEN];  /* ADMINPASS（留空=无密码） */
    wchar_t  defaultmodel[CFG_KEY_LEN];
    int      topmost;                 /* TOPMOST：主窗口置顶初始状态（1=开启[缺省]，0=关闭） */
    CFGMODEL models[CFG_MAX_MODELS];
    int      nmodels;
} CONFIG;

/* 解析 ini（编码由 util.c 的 text_decode 统一识别：UTF-8±BOM / UTF-16LE/BE±BOM /
 * 无 BOM UTF-16 启发式 / GBK；损坏 UTF-8 与二进制拒绝）。
 * 成功返回 TRUE；失败返回 FALSE 并把具体原因写日志（读文件/编码/无机型节/超限）。
 * 解析期的容量超限、截断、畸形/空/重复/超长节头、PRJNAME 不匹配等写 LOG_WARN。 */
BOOL cfg_load(const wchar_t* ini_path, CONFIG* cfg);

/* 取键值（m/key 为 NULL 时返回 NULL，不崩溃） */
const wchar_t* cfg_get(const CFGMODEL* m, const wchar_t* key);
/* 取键值 + 默认值（out 为 NULL 或 outlen==0 时安全返回；全程 size_t） */
void cfg_get_def(const CFGMODEL* m, const wchar_t* key,
                 const wchar_t* def, wchar_t* out, size_t outlen);
int  cfg_get_int(const CFGMODEL* m, const wchar_t* key, int def);

/* [TOOLSINFO] 内部使用 */
const wchar_t* cfg_root_get(const CONFIG* c, const wchar_t* key);

/* 功能开关快捷判断 */
BOOL cfg_enabled(const CFGMODEL* m, const wchar_t* key); /* ENABLE_* 类键 */

/* 按内部键/显示名查找机型（找不到返回 NULL） */
CFGMODEL* cfg_find_internal(CONFIG* c, const wchar_t* internal);
CFGMODEL* cfg_find_display(CONFIG* c, const wchar_t* display);

/* 修改机型单键并写回 ini。
 * 只支持机型节；键存在则就地替换，不存在则插入目标节节尾（节不存在返回 FALSE）。
 * 保留原 key 拼写/前导空白/行内注释（含注释前空白）、原文件编码与 BOM
 * （UTF-8±BOM / UTF-16LE/BE±BOM / GBK）、逐行原行尾风格；值允许 ';'。写前校验、写后回读（目标键==新值且键数不变），
 * 任何一步失败返回 FALSE，失败路径删除临时文件且不破坏原文件。
 * 拒绝含 \r/\n 的 key/newval，以及首尾空白、超长（>=CFG_VAL_LEN/CFG_KEY_LEN）、
 * 会被 cfg_load 当注释截断的值。 */
BOOL cfg_save_key(const wchar_t* ini_path, const wchar_t* section,
                  const wchar_t* key, const wchar_t* newval);

#endif
