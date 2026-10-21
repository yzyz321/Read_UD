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
    CFGMODEL models[CFG_MAX_MODELS];
    int      nmodels;
} CONFIG;

/* 解析 ini（UTF-8/UTF-16/ANSI 自动识别）。成功返回 TRUE */
BOOL cfg_load(const wchar_t* ini_path, CONFIG* cfg);

/* 机型节内取键值（未找到返回 NULL） */
const wchar_t* cfg_get(const CFGMODEL* m, const wchar_t* key);
/* 取键值 + 默认值 */
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

/* 修改机型单键并写回 ini（保留文件中其余行与行内注释） */
BOOL cfg_save_key(const wchar_t* ini_path, const wchar_t* section,
                  const wchar_t* key, const wchar_t* newval);

#endif
