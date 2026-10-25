/* ============================================================
 * version.h - UdiskTools 版本信息
 *
 * 两套版本相互独立，勿混淆：
 *   1) 软件版本  APP_VERSION / APP_VERSION_W / APP_VERSION_MAJOR..BUILD
 *      —— 程序自身版本，显示在 --version、启动日志、窗口标题“软件 vX.Y.Z”，
 *         并写入 res/udisktools.rc 的 FILEVERSION/PRODUCTVERSION（X.Y.Z.0）。
 *   2) 配置文件版本  ini [TOOLSINFO] VERSION
 *      —— 仅配置内容的版本标记（配置窗口/启动日志显示“配置版本”），
 *         与软件版本无关，不随程序升级自动修改。
 * ============================================================ */
#ifndef UDK_VERSION_H
#define UDK_VERSION_H

#define APP_TITLE     "UdiskTools"
#define APP_VERSION   "1.2.0"          /* 软件版本（字符串） */

/* 宽字符版本（使用宏拼接） */
#define APP_TITLE_W   L"U盘拷盘工具"
#define APP_VERSION_W L"1.2.0"         /* 软件版本（宽字符） */

/* 数字版本（res/udisktools.rc 的 FILEVERSION/PRODUCTVERSION 使用；
 * 必须与上面的 APP_VERSION 字符串保持一致，FileVersion 形如 1.2.0.0） */
#define APP_VERSION_MAJOR 1
#define APP_VERSION_MINOR 2
#define APP_VERSION_PATCH 0
#define APP_VERSION_BUILD 0

/* 构建日期/时间（编译器预定义宏；供启动日志与 --version 展示，不参与业务逻辑。
 * 注意：构建时间写入日志/--version，不写入 res 的 VERSIONINFO，避免构建产物元数据漂移。 */
#define APP_BUILD_DATE __DATE__
#define APP_BUILD_TIME __TIME__

#endif /* UDK_VERSION_H */
