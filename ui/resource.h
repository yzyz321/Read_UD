/* ============================================================
 * ui/resource.h - 全部控件与窗口 ID 登记（后期交互/测试统一引用）
 * 命名规范：窗口 WND_，区域 PANE_，控件 IDC_，热键 ID_HOTKEY_
 * ============================================================ */
#ifndef RESOURCE_H
#define RESOURCE_H

/* ---------- 主窗口 ---------- */
#define WND_MAIN            100
#define WND_CONFIG          101

/* ---------- PANE_TOP ---------- */
#define IDC_COMBO_MODEL     1001   /* 机型下拉框（显示 DISPLAYNAME）            */
#define IDC_BTN_START       1002   /* 开始（默认名，可被 BTN_START 覆盖显示）   */
#define IDC_BTN_STOP        1003   /* 停止                                     */
#define IDC_BTN_FORMAT      1004   /* 格式化                                   */
#define IDC_BTN_READLABEL   1005   /* 读取标签                                 */
#define IDC_BTN_WRITELABEL  1006   /* 写入标签（机型可覆盖显示名，如"升级"）    */
#define IDC_BTN_CONFIG      1007   /* 配置 → WND_CONFIG                        */
#define IDC_CHK_TOPMOST     1008   /* 窗口置顶复选框 HWND_TOPMOST/NOTOPMOST    */
#define IDC_STC_MODE        1009   /* 手动/自动模式标记                        */

/* ---------- PANE_LABEL ---------- */
#define IDC_STC_LABEL       1101   /* 标签信息行                               */

/* ---------- PANE_STATUS ---------- */
#define IDC_STC_DEVICE      1201   /* 设备状态                                 */
#define IDC_STC_ERROR       1202   /* 出错徽标                                 */

/* ---------- PANE_TASK ---------- */
#define IDC_PBAR_TASK       1301   /* 进度条                                   */

/* ---------- PANE_CMDS ---------- */
#define IDC_EDIT_ENGINE     1401   /* CMD-A 操作引擎输出                       */
#define IDC_EDIT_IDENT      1402   /* CMD-B 标识生成输出                       */

/* ---------- PANE_FILE ---------- */
#define IDC_STC_READVERDICT 1501   /* 读取判定徽标 PASS/FAIL                   */
#define IDC_EDIT_FILE       1502   /* READFILE 内容显示                        */

/* ---------- 热键 ---------- */
#define ID_HOTKEY_T         2001   /* T = 读写测试                             */
#define ID_HOTKEY_C         2002   /* C = 写入                                 */

/* ---------- 配置窗口 WND_CONFIG（IDC_CFG_ 前缀） ---------- */
#define IDC_CFG_LIST_MODELS 3001   /* 机型列表（显示 DISPLAYNAME）             */
#define IDC_CFG_EDIT_PASS   3002   /* 密码输入（ES_PASSWORD）                  */
#define IDC_BTN_UNLOCK      3003   /* 解锁编辑                                 */
#define IDC_BTN_SAVECFG     3004   /* 保存                                     */
#define IDC_BTN_CLOSECFG    3005   /* 关闭                                     */
#define IDC_STC_CFGSTATE    3006   /* 只读/已解锁状态                          */
#define IDC_EDIT_CFGTEXT    3007   /* 配置内容展示（只读）                     */
#define IDC_EDIT_CFGKEY     3008   /* 待修改键名（解锁后可用）                 */
#define IDC_EDIT_CFGVAL     3009   /* 待修改键新值（解锁后可用）               */

#endif /* RESOURCE_H */
