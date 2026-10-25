/* ============================================================
 * pattern.h - 通配符模式匹配（WILDCARD：* ? 与捕获）
 * ============================================================ */
#ifndef UDK_PATTERN_H
#define UDK_PATTERN_H

#include <windows.h>

/*
 * 通配符匹配：* 任意长串（可为空），? 恰一个字符
 * 大小写：ASCII 大小写不敏感（文件名语义，与 FindFirstFileW 一致）
 * cap1（可空）：所有 '?' 消耗的字符拼接（用于捕获可变部分，如 GTB907785???? 中 XXXX）；
 *               容量不足时捕获被截断，但匹配结论仍有效
 * pattern/name 为 NULL：返回 FALSE（cap1 若有效会置空串）
 * 返回：匹配=TRUE
 */
BOOL pat_match_capture(const wchar_t* pattern, const wchar_t* name,
                       wchar_t* cap1, size_t cap1len);

/*
 * 可判截断的捕获接口（P2-16 残余）：
 *   与 pat_match_capture 语义相同（匹配结论不受捕获缓冲大小影响），
 *   额外通过 *truncated（可 NULL）报告捕获文本是否被截断：
 *     - 匹配成功且捕获所需字符数 > cap1 可容纳字符数（caplen-1）→ TRUE；
 *       cap1 仍为 NUL 结尾的前缀；
 *     - 匹配失败 / 未提供 truncated / cap1==NULL 或 caplen==0 → FALSE。
 * cap1==NULL 或 caplen==0 时安全（不写缓冲），pattern/name NULL 返回 FALSE 并清空 cap1。
 */
BOOL pat_match_capture_ex(const wchar_t* pattern, const wchar_t* name,
                          wchar_t* cap1, size_t cap1len, BOOL* truncated);

/* 简单匹配（无捕获） */
BOOL pat_match(const wchar_t* pattern, const wchar_t* name);

#endif
