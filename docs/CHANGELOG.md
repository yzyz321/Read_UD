# 变更记录（CHANGELOG）

> 本文件按批次记录主题级变更，**不重复**审计报告中的逐条发现与证据；逐条状态见 [代码审计报告（2026-10-24）](UdiskTools-代码审计报告-20261024.md)。
> 预留/未实现项见 [UI 功能状态说明](UI功能状态说明.md) §7；本文件不写未验证承诺。

## [2026-10-25] 软件 v1.2.0 · 注释完善 + 流程图更新 + 用户使用手册

### 版本
- 软件版本升至 **1.2.0**（`src/version.h` `APP_VERSION`/数字宏；`res/udisktools.rc` VERSIONINFO 由宏驱动自动跟随 1.2.0.0）；构建产物 `bin/udisktools_v1.2.0.exe`。配置文件版本仍独立（ini `[TOOLSINFO] VERSION`）。
- 文档当前状态区同步 1.2.0（README、docs/README、开发流程、规划书头部、UI 功能状态说明、配置文件使用手册、控件位置对照图）；CHANGELOG/审计报告中的历史记录保留原文不改。

### 注释
- `src/gui_config.c` / `src/gui_shared.h` 文件头补架构说明（拆分边界、共享符号契约）；`gui.c` / `main.c` / `config.c` 关键变更点（ini 缺失启动交互、TOPMOST 置顶同步、标题双版本）注释补全。

### 文档
- 新增面向最终用户的《[用户使用手册](用户使用手册.md)》（安装启动/界面图解/典型操作/配置文件/常见问题）。
- 《插拔全流程图》更新：启动段补"ini 缺失 → 弹窗二选一（默认配置运行 / 退出）"分支与 TOPMOST 置顶；模块图补 `gui_config.c` / `gui_shared.h` 拆分。

## [2026-10-24] 文档结构扁平化与去重

### 目录拍平
- `docs/` 子目录 `user/`、`reference/`、`design/`、`design/mockup/`、`development/`、`audit/` 全部上移到 `docs/` 根（项目规模较小，不细分目录）；仅保留 `archive/` 一个历史归档子目录。
- 全部文档互链同步修正（本文件、UI 功能状态说明、开发流程、规划书、两个 README）。

### 去重（消除双份维护）
- 《软件规划书》§7（开发流程）与《开发流程.md》大段重复 → 规划书精简为要点摘要 + 指向《开发流程.md》（后者为流程权威文档）。
- 《软件规划书》§4.5 逐控件 ID 全表与《UI功能状态说明.md》重复 → 规划书保留界面分区概览 + 指向《UI功能状态说明.md》（后者为逐控件权威文档，53 个 ID 全覆盖）。

### 界面与构建调整（2026-10-24 补）
- **窗口标题格式调整**：`<TITLE>（软件 v<软件版本>）（配置 v<配置版本>）`（软件版本在前、配置版本加"配置"前缀；配置版本为空时省略"（配置 v…）"），实测 `U盘拷盘工具（软件 v1.1.0）（配置 v1.0.0）`。
- **构建产物带版本号**：Makefile 产物改为 `bin/udisktools_v<软件版本>.exe`（版本号 sed 提取自 `src/version.h` `APP_VERSION`）；CI 冒烟测试与 artifact 路径同步。
- **图标/资源说明**：产物必须经 `windres` 链接 `res/udisktools.rc`（图标+VERSIONINFO+manifest）——绕过 Makefile 用裸 gcc 编译会丢失图标；已验证新产物图标 1/1 嵌入、VERSIONINFO 完整。
- 相关文档同步：README、docs/README、开发流程、UI功能状态说明、控件位置对照图。

### 已同步（2026-10-24 补）
- `src/gui.c:1095` 提示文案旧路径 `docs/user/配置文件使用手册.md` → `docs/配置文件使用手册.md`（编译零警告，随本批提交）。
- 《控件位置对照图》同步 v1.1.0 界面：第二行补 AUTO_COPY 1009 / AUTO_TEST 1018 灰色只读状态框、快捷键显示文字更新；状态行补计数器 1017；新增 READRULE 1109 判定规则栏一行；标题栏改为"TITLE v配置版本（软件 v1.1.0）"格式；说明表新增 1009/1018/1109 三条行为说明。

## [2026-10-25] 内置默认配置与 ini 缺失启动交互

### 新增（TOPMOST 键）
- **窗口置顶默认开启 + 可配置**：`[TOOLSINFO]` 新增全局键 **`TOPMOST`**（`0/1`，缺省 `1`=启动即置顶）——`IDC_CHK_TOPMOST 1008` 初始勾选状态与 `HWND_TOPMOST/NOTOPMOST` 由该键决定；配置保存/机型切换重载时同步复选框与置顶状态；非法值按缺省 1 处理并写告警日志。`profiles/UdiskTools.ini`、`profiles/UdiskTools_default.ini` 均加入 `TOPMOST=1`，`default_ini.h` 已重生成。
- 实测：截图确认复选框默认勾选、`WS_EX_TOPMOST` 生效，日志含 `[SYS] 窗口置顶初始状态：开启（TOPMOST=1）`；回归脚本 `scripts/test_topmost.ps1`。
- 文档同步：使用手册（键表 §2/§8.1）、UI 功能状态说明、控件位置对照图。

### 新增
- **ini 缺失启动交互**：`UdiskTools.ini` 不存在时启动弹窗二选一——**是**=以内置默认配置运行（`main.c` 新增 `write_builtin_default_ini()`：临时文件+FlushFileBuffers+MoveFileEx 原子写入后重载，继续正常启动）；**否**=直接退出（退出码 1）。文件存在但解析失败仍走原有错误退出路径；`--check-ini`（CLI）行为不变（缺失=1，不弹窗）。全流程写日志（选择与写入结果）。
- **内置默认内容更换**：内嵌默认配置由 `profiles/UdiskTools.ini`（4 机型）改为 **`profiles/UdiskTools_default.ini`**（1 机型通用配置，与发布包 `UdiskTools_default.ini` 同内容）；`gen_default_ini.py/.ps1` 生成源同步切换，CI `check-default-ini` 门禁沿用。
- **影响**：配置窗口 **[恢复默认设置]** 现在恢复的是这份 1 机型默认配置（与 ini 缺失时启动写入的内容一致，两处共用同一内嵌常量 `DEFAULT_INI_UTF8`）。

### 验证
- 场景①：ini 存在 → 正常启动无弹窗，标题/机型下拉正常。
- 场景②：ini 缺失 → 弹窗出现；选"是"→ 程序目录生成 `UdiskTools.ini`（内容与内置默认逐行一致，PRJNUM=1）并以默认配置启动。
- 场景③：ini 缺失 → 选"否"→ 不生成文件、进程退出（码 1）。
- 回归脚本：`scripts/test_missing_ini.ps1`（跨进程查找 `#32770` 弹窗并回发 WM_COMMAND IDYES/IDNO）。

### 重构（gui.c 拆分）
- **主程序 GUI 与配置 GUI 分离**：`src/gui.c`（原 2447 行 → **1817 行**，保留主窗口全部职责）+ 新建 `src/gui_config.c`（约 **680 行**，配置窗口整块：键说明表、枚举值、机型编辑、锁定态、[保存修改]/[恢复默认设置]、`cfg_register_class` 注册 `UDK_CFGCLASS`）+ 新建 `src/gui_shared.h`（内部共享符号：`g_hinst/g_hmain/g_hcfglist/g_cur_model/g_font*`、`dpi_scale/dpi_update_font/refresh_buttons/update_main_title/reload_config_and_ui/open_config_dialog/cfg_rebuild_model_list`）。
- **构建同步**：`Makefile`/`CMakeLists.txt`/`.github/workflows/build.yml` 源清单加入 `gui_config.c`；`tests/integration/test_gui_state.c` 改为 `#include "gui.c"` + `#include "gui_config.c"` 双包含继续测 static 函数。
- **全量验证**：gcc/clang 双编译零警告、完整链接 LINK_OK、CLI 冒烟 `--check-ini` 正常；测试套件 **TOTAL PASS=11 FAIL=0**（test_gui_state PASS=32；test_encoding 内嵌默认配置一致性 PASS=71 FAIL=0，`default_ini.h` 重新生成以含 TOPMOST 行）。
- **文档同步**：规划书（模块结构补 gui_config.c/gui_shared.h）、README（src 清单）、UI 功能状态说明（依据代码与窗口类位置）。

## [2026-10-24] 功能优化 · 软件 v1.1.0

### 版本体系
- 软件版本升至 **1.1.0**（`src/version.h` `APP_VERSION`/数字宏、`res` VERSIONINFO 1.1.0.0）；配置文件 `[TOOLSINFO] VERSION` 明确为“**配置文件版本**”，与软件版本相互独立。
- 窗口标题改为 `<TITLE> v<配置版本>（软件 v<软件版本>）`（配置版本为空时省略 `v<...>`）；启动日志显示“软件 v1.1.0”；`--version` 显示软件版本/FileVersion/构建时间，`--check-ini` 显示配置文件版本。

### 界面
- 新增 **AUTO_COPY/AUTO_TEST 灰只读状态框**（`IDC_CHK_AUTO_COPY` 1009 / `IDC_CHK_AUTO_TEST` 1018）：随当前机型实时显示配置值，不可编辑；不参与 AUTO/MANUAL 判定。
- 新增 **READRULE 判定规则标签**（`IDC_STC_READRULE` 1109）：`AUTO_TEST=1` 时显示 `判定规则：<READRULE>`（空规则显示“（未配置 READRULE）”），`AUTO_TEST=0` 时为空；仅展示，不改变判定逻辑。
- 快捷键复选框文字缩短为“快捷键使能（Ctrl+Alt+T/C）”，行为不变。

### 文档 / CI
- README、使用手册、UI 功能状态说明、规划书同步版本、标题与新控件；文档索引/互链同步更新。
- CI CLI 冒烟：`--version` 期望 token 更新为 **1.1.0**；`--check-ini` 增加“配置版本”断言（配置文件版本）。

### 临时目录
- `temp_run/`（工作区根目录、仓库外）为**临时调试/测试运行目录**：不纳入构建/测试清理流程，文档说明但**禁止删除/改动**。

## [2026-10-24] 审计后修复批次（第 1–7 批）· v1.0.0

### 00. 基线与审计（2026-10-23 ~ 10-24）
- 完成代码审计并建立报告基线：`docs/audit/UdiskTools-代码审计报告-20261024.md`（副本；原件在仓库外保留）。
- 审计范围：`src/*.c`、`ui/resource.h`、`res/`、`profiles/`、`scripts/`、`.github/workflows/`、`docs/`；结论分 P0/P1/P2/P3 与工程化项。
- 本批起按第 1–7 批分批修复/加固；批次主题如下。

### 第 1 批：配置写回 / 格式化安全 / 停止链路
- `cfg_save_key` 重写：消除截断/崩溃/CR 累积，保留原编码与 BOM、行尾、行内注释，新键插入目标节，写后回读校验，失败保留原文件。
- READRULE 行内注释规则统一（`;` 前有空白才算注释，规则链 `MUST_CONTAIN:..;MIN_LEN:..` 可正常解析）。
- 格式化：强制人工确认（忽略 `CONFIRM=0`），确认前后设备身份复核，格式化后刷新卷信息与界面。
- 停止链路：任务/单操作启动即刷新按钮状态（[停止] 可用），工作线程句柄 join，退出先停止再等待，`CopyFileExW` 接入取消标志。

### 第 2 批：结果语义 / 设备事件 / 错误分类
- 统一结果码 `OP_OK / OP_FAIL / OP_STOPPED / OP_SKIP_CFG`：“按配置正常屏蔽”按成功计数；程序自行降级/未实现/无法校验判失败。
- 设备事件协议改为“变化盘符位图 + 插入/拔出”，同周期先拔后插不丢事件；任务中插入记为待处理盘，成功/按配置跳过结束后补执行。
- 换盘/设备丢失错误分类（21/1167/1112 等）与日志提示。

### 第 3 批：USB 识别 / rwtest / MD5 / 格式化刷新
- `usbinfo` 改为 USB Hub IOCTL 按 VID/PID 匹配端口识别链路速度；取不到标注“未知（原因）”不报错。
- `rwtest` 使用 `NO_BUFFERING/WRITE_THROUGH` + 扇区对齐直读介质（回退缓冲时告警），测试文件带 PID、用完删除。
- 复制 MD5：无法计算判失败；不一致删除坏副本并重试一次；仍不一致判失败。
- 格式化：`FILESYSTEM`/`VOLUME` 预校验，写卷标失败判失败，格式化后刷新设备/标签行；`label_read` 对未就绪盘判失败。

### 第 4 批：编码/解析 / 配置快照 / 默认配置与 CI
- 文本编码统一探测（UTF-8±BOM、UTF-16LE/BE±BOM、无 BOM 启发式、GBK；损坏 UTF-8/二进制拒绝并给原因），ini 解析容量/畸形/重复节改为 WARN 不静默。
- 任务/单操作使用机型配置**按值快照**，消除 worker 与 UI 的 `g_cfg` 并发；`reload` 堆分配（消除约 906KB 栈帧）。
- 保存/恢复默认：失败不再假报成功，恢复默认先备份 `.bak` + 原子写 + 回读校验；重载后刷新标题/机型列表。
- 以 profiles 重新生成 `src/default_ini.h`，新增 `scripts/gen_default_ini.ps1`（无参生成 / `-Check`），CI 增加默认配置一致性门禁。

### 第 5 批：日志 / 单实例 / CLI / 资源
- 日志：句柄常驻按天缓存、共享读写删除、>8MB 轮转 `.log.1`、被占用写 `_alt.log` 不丢行、`log_shutdown` 收尾；日志初始化并发安全。
- `PostMessage` 堆串统一走 `post_owned_string`（失败自释放）；UI 线程设备信息读取改走 `dev_get_info_volume`（不跑 SetupAPI）；消息循环 `GetMessage==-1` 与退出码（0/1/3）处理。
- 单实例互斥 + 激活现有窗口；CLI `--ini` / `--check-ini` / `--version` / `--help`；崩溃转储 `MiniDumpWriteDump`。
- `res/` 加入 `.rc` 版本信息、图标、manifest（comctl32 v6 / PerMonitorV2 / longPathAware），构建接入 windres/dbghelp。

### 第 6 批：UI 状态一致性 / 热键 / 无密码锁定 / IDENT 预留
- 统一任务界面复位（判定/内容/命中行/进度/徽标），进度百分比单一入口 `post_progress_pct`；单操作完成文案区分。
- 标题不截断（1200 缓冲）；配置窗口初始选中当前机型、枚举非法值安全回退、DPI 字体刷新（避免悬空 HFONT）；最小窗口尺寸；RichEdit 三级回退（50W→20W→STATIC）。
- 多盘设备行/标签行有界追加（8192，不越界）；STATIC 颜色改 `WM_CTLCOLORSTATIC`。
- 热键改为 **Ctrl+Alt+T=读写测试 / Ctrl+Alt+C=复制**（单操作，需勾选“快捷键使能”，任务中忽略并记日志）。
- **按用户要求取消“密码连错 3 次锁定”**：密码错误只提示 + 日志、可无限重试；未解锁只读、保存/恢复默认不可用。
- `IDC_EDIT_IDENT 1402`（CMD-B）明确为**预留（标识生成扩展）**：无入口、不参与任务。
- 文档纠偏：README/使用手册/规划书删除 3 次锁定承诺、CMD-B/标识生成改预留；新增 [UI 功能状态说明](UI功能状态说明.md)（resource.h 全部 50 个 ID 覆盖）。
- 另：`op_write` 先匹配源文件再建目标目录；`${MATCH_1}` 捕获截断显式上报。

### 第 7 批：文档目录化 / 测试与 CI 门禁（本批）
- docs 目录化：`user/`（使用手册）、`reference/`（UI 功能状态说明）、`design/`（规划书、插拔流程图、mockup）、`development/`（开发流程）、`audit/`（审计报告副本）、`archive/`（保持原样）。
- 新增 [文档索引](README.md)、[开发流程](开发流程.md)、本 CHANGELOG；修正 README 顶部死链与全部文档互链。
- 审计报告副本入库 `docs/audit/`（原件保留在仓库外，内容不改）。
- 测试入库与 CI（B 线）：新增 `tests/unit`、`tests/integration` 用例；CI 增加 “Run tests”（由 `tests/run_tests.ps1` 驱动）与 “Smoke test (CLI)” 门禁（详见 [开发流程](开发流程.md) §5）。

## 说明与边界

- 本文件中的“已加入/已修复”以**实际代码与 CI 文件**为准；预留项（CMD-B、独立写标签、REGEX、PROCESSED、ADMINPASS_HASH、多盘并行/失败策略/CSV/Type-C/WM_DEVICECHANGE/设备白名单等）**未承诺可用**，详见 [UI 功能状态说明](UI功能状态说明.md) §7 与 [规划书](UdiskTools软件规划书.md) §3/§6。
- 历史版本规划与旧方案见 [archive/](archive/)（仅历史参考，已作废，不作现行依据）。
- 文档中的行号/路径以当前工作树为准；后续代码变动时请同步更新相关文档（维护规则见 [文档索引](README.md) §3）。
