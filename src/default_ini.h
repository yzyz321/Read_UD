/* 自动生成：内嵌默认配置（ini 缺失时启动 / 配置窗口[恢复默认设置]共用） */
/* 来源：profiles/UdiskTools_default.ini  生成方式：转义为 C 字符串（UTF-8） */
#ifndef UDK_DEFAULT_INI_H
#define UDK_DEFAULT_INI_H

static const char* DEFAULT_INI_UTF8 =
"[TOOLSINFO]\n"
"TITLE=U盘拷盘工具\n"
"VERSION=1.0.0\n"
"ADMINPASS= 1234                     ; 配置窗口管理员密码（或改用 ADMINPASS_HASH，二选一；留空=无密码保护）\n"
"TOPMOST=1                       ; 主窗口置顶初始状态：1=启动即置顶（缺省）；0=启动不置顶（仍可点复选框切换）\n"
"PRJNUM=1\n"
"PRJNAME1=GENERIC\n"
"DISPLAYNAME1=通用机型\n"
"DEFAULTMODEL=GENERIC            ; 程序启动时默认选中的机型\n"
"\n"
"\n"
"; ------------------------------------------------------------\n"
"; 机型 1：通用机型\n"
"; 功能：写入文件 + 格式化 + 读取并显示 readme.txt\n"
"; 按钮显示名：可将任一按钮的显示文字按机型自定义（如\"写入标签\"→\"升级\"）\n"
"; ------------------------------------------------------------\n"
"[GENERIC]\n"
"ENABLE_READ=1\n"
"ENABLE_WRITE=1\n"
"ENABLE_FORMAT=1\n"
"ENABLE_USBINFO=1\n"
"AUTO_COPY=0                     ; 自动复制：1=插盘自动复制（需 AUTORUN=1 或开启监控）；0=仅手动点[复制]\n"
"AUTO_TEST=0                     ; 自动测试：1=插盘自动执行读写测试；0=仅手动点[读写测试]\n"
"BTN_WRITELABEL=升级             ; 按钮显示名覆盖（缺省时用默认名\"写入标签\"）\n"
"; BTN_START=开始                ; 其余按钮同理：BTN_START/BTN_STOP/BTN_FORMAT/BTN_READLABEL/...\n"
"WRITEMODE=COPY                  ; 简单复制\n"
"SRCPATH=.\\upgrade_GENERIC\\*.*          ; 写入源（支持通配符；按现场路径修改）\n"
"DSTPATH=\\upgrade\\                       ; 复制到 U 盘根目录\n"
"READFILE=SYSTEM\\PAW6K-LSN\n"
"MATCH_POLICY=NEWEST\n"
"READPATTERN=WILDCARD\n"
"READRULE=MIN_LEN:1              ; 通用机型仅要求文件非空，可按需追加 MUST_CONTAIN:关键字\n"
"CONFIRM=1                       ; 格式化前弹窗确认\n"
"FILESYSTEM=FAT32\n"
"VOLUME=                         ; 格式化/写标签时的卷标，按需填写\n"
"HASH=md5\n"
;

#endif
