/* ============================================================
 * pattern.h - 通配符模式匹配（WILDCARD：* ? 与捕获）
 * ============================================================ */
#ifndef UDK_PATTERN_H
#define UDK_PATTERN_H

#include <windows.h>

/*
 * 通配符匹配：* 任意长串，? 恰一个字符
 * cap1（可空）：所有 '?' 消耗的字符拼接（用于捕获可变部分，如 GTB907785???? 中 XXXX）
 * 返回：匹配=TRUE
 */
BOOL pat_match_capture(const wchar_t* pattern, const wchar_t* name,
                       wchar_t* cap1, size_t cap1len);

/* 简单匹配（无捕获） */
BOOL pat_match(const wchar_t* pattern, const wchar_t* name);

#endif
