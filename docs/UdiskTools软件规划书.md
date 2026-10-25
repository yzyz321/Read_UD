# UdiskTools 软件规划书（最终合并版）

> 版本：v2.2 ｜ 日期：2026-10-24 ｜ 状态：按 **软件 v1.2.0** 实际实现修订（含“配置文件版本 vs 软件版本”说明）
> 本文件是以下四份策划文档的**唯一有效合并版**，与之冲突的旧描述一律作废：
> ①《策划方案-UdiskOps可移动磁盘操作软件》v1.0 → ②《策划方案-UdiskTools合并重构(C语言版)》v2.0
> → ③《UdiskTools软件规划书》v1.0 → ④《UdiskTools规划与开发流程》v1.1（原件存 `docs/archive/`）
> 定位：Windows 可移动磁盘（U 盘）自动化操作工具 · C 语言全新开发
> v2.2 说明：与《开发流程.md》《UI功能状态说明.md》去重——界面逐控件细节与工程流程细节改为指向权威文档，本文专注规划/需求/规格/验收。

---

## 0. 文档沿革

| 版本 | 日期 | 文档 | 结局 |
|---|---|---|---|
| v1.0 构想 | 2026-10-20 | 策划方案-UdiskOps（Python + PySide6 + JSON/YAML） | 被 v2.0 合并重构方案取代，未开发 |
| v2.0 定向 | 2026-10-20 | 策划方案-UdiskTools合并重构(C语言版) | 确立 C/Win32 + ini 重构路线，含旧软件功能分析 |
| v1.0 定稿 | 2026-10-21 | UdiskTools软件规划书 | 详细规格定稿（待开发状态），内容并入本文 |
| v1.1 修订 | 2026-10-23 | UdiskTools规划与开发流程 | 首次按 alpha.19 实际实现修订，并入本文 |
| v2.0 合并 | 2026-10-23 | 本文件 | 四份策划文档合并为唯一有效规划文档 |
| **v2.2 去重** | **2026-10-24** | **本文件** | **按软件 v1.2.0 修订；§4.5/§7 细节改为指向《UI功能状态说明》《开发流程》** |

### 采纳与取舍（来自 UdiskOps v1.0 构想）
| UdiskOps 方案 | 处置 | 现行落点 |
|---|---|---|
| 配置驱动、自动/手动双模式、强制留痕 | ✅ 采纳 | ini 配置 + 监控/AUTORUN + 日志双通道 |
| 读写校验（MD5） | ✅ 采纳 | 复制逐文件 MD5 + 读写测试比对 |
| Python/PySide6、JSON/YAML、Schema 校验 | ❌ 否决 | C11 + 纯 Win32 + ini（单 exe 免运行库） |
| 精准测速（speed_test、测速曲线） | ❌ 否决 | 改为 USB 通讯模式识别（usbinfo） |
| HTML/JSON 报告、SQLite 存档、托盘常驻、Web 界面 | ❌ 否决 | CMD-A 日志输出区（CMD-B 输出区预留）+ 按日期滚动日志 |
| 设备白名单/黑名单 | ⏸ 未实装 | 列为预留（按 VID/PID/SN 过滤） |
| 多盘并行、失败策略 stop/continue/retry | ⬜ 待办 | Phase 3 |

---

## 1. 项目沿革与旧软件分析

### 1.1 旧软件（仅作功能模式参考，完全重构、无迁移负担）

**DiskRW_V2.0（IET_disktools，exe + ini）**——产线 U 盘拷盘/格式化工具：

| 功能 | ini 键 | 新软件落点 |
|---|---|---|
| 多项目配置 | `PRJNUM` / `PRJNAME1..n` | ini 节结构（内部键 + DISPLAYNAME 双层映射） |
| 文件复制 | `SRCPATH` → `DSTPATH` | op_write（WRITEMODE=COPY，按字节进度 + MD5） |
| 插盘自动读写/复制 | `AUTORW` / `AUTOCOPY` | 监控开关 + `AUTORUN`（三层职责分离，见 3.2） |
| 格式化 | `CONFIRM` / `FILESYSTEM` / `VOLUME` | op_format（fmifs.dll FormatEx） |
| 热键 T / C | 键盘热键 | RegisterHotKey（Ctrl+Alt+T=读写测试 / Ctrl+Alt+C=复制，均为单操作） |

**read-U（Python + tkinter）**——U 盘文件监控读取器：

| 功能 | 新软件落点 |
|---|---|
| 1s 轮询 + `GetDriveType == DRIVE_REMOVABLE` | 设备层轮询（沿用，WM_DEVICECHANGE 升级列为待办） |
| 按文件名读取并显示（多编码） | op_read + READRULE 判定（先判定再显示，双窗格） |
| 拔盘/文件移除清空显示 | 插盘先清空上一轮状态再刷新 |

**合并思路（已实现）**：统一为一个配置驱动的引擎，"复制/格式化/读写测试"与"读文件显示判定"都成为配置里的步骤，一个 GUI 全部承载。

### 1.2 迭代历程（v1.0.0-alpha.1 → alpha.19）
alpha.11 配置键值提示 → alpha.12–14 判定/刷新/徽标修正 → alpha.15 计数器与按钮统一 → alpha.16 READ 子目录修复 + 按字节进度 + 双文件窗格 → alpha.17 VID/PID/SN 设备链 → alpha.18 配置值输入完善 + 键值编辑方式对照表 → alpha.19 修复键名下拉选择时值编辑器不切换（CBN_SELCHANGE 文本滞后）。

---

## 2. 项目概述

### 2.1 定位
**UdiskTools** 是一款 Windows 单文件 GUI 工具（纯 Win32 API，无第三方运行时依赖）：一个 exe + 一个 ini。通过配置文件定义每种机型的操作流程，支持**自动**（插盘触发）与**手动**（按钮/热键）两种执行方式，覆盖复制写入、读取判定、格式化、USB 通讯模式识别、标签读取、读写测试等能力，全程强制留痕（CMD-A 日志输出区 + 日志文件；CMD-B 输出区为预留）。

### 2.2 核心设计原则
| 原则 | 说明 |
|---|---|
| 配置驱动 | 操作内容、功能开关、判定规则全部来自 ini，改配置不改代码 |
| 双层机型命名 | 内部键（程序结构依赖，永不改）与显示名（操作员可见，随时改）分离 |
| 权限最小化 | 日常操作普通用户权限即可；仅格式化按需提权 |
| 强制留痕 | 所有操作写日志，不可关闭；界面 CMD 区与文件日志双通道 |
| 高 DPI 优先 | Per-Monitor V2 感知 + `WM_DPICHANGED` 动态重排 |

### 2.3 运行环境
- Windows 10 / 11 x64
- 编译工具链：**MinGW-w64（gcc / llvm-mingw 均可）**，Makefile 构建（CMakeLists.txt 备用）
- 交付物：单 exe（免安装、免运行库）+ 示例 ini + 使用说明

---

## 3. 实施状态总览（截至软件 v1.2.0）

主体功能已全部实装并通过真盘验证（READ 子目录命中、复制进度、配置编辑等）；
版本按 `v1.0.0-alpha.N` → `v1.2.0` 迭代发布。**软件版本（`src/version.h`）与配置文件版本（ini `[TOOLSINFO] VERSION`）相互独立**：前者随程序升级，后者只标记配置内容。

| 状态 | 内容 |
|---|---|
| ✅ 已实装 | 插拔轮询与设备信息（含 VID/PID/SN）、整任务编排（识别→复制→读取判定→测试）、复制（按字节进度+MD5 校验）、读取判定（子目录/通配/策略/规则链/双窗格）、格式化（fmifs + 确认 + 卷标）、USB 速度识别、读取标签、读写测试、配置窗口（下拉/输入/解锁/恢复默认）、CMD-A 日志输出区（CMD-B 输出区预留）、监控窗格与插拔计数、快捷键 Ctrl+Alt+T=读写测试 / Ctrl+Alt+C=复制、**AUTO_COPY/AUTO_TEST 只读状态框（1009/1018）与 READRULE 判定规则标签（1109，AUTO_TEST=1 显示）**、标题同时显示配置版本与软件版本、置顶、DPI 动态重排、CI 工作流（.github/workflows/build.yml）、单实例/CLI（--ini/--check-ini/--version/--help）、崩溃转储、.rc 版本资源/图标/manifest |
| ⏸ 预留（v1.x 后续） | WRITEMODE=PROCESSED、READPATTERN=REGEX、Type-C 正反插识别、gen_ident 按台标识生成（预留：CMD-B 输出区预留，无入口/不参与任务）、ADMINPASS_HASH、设备白名单、独立 verify 操作（MD5 已内置于复制）、独立 write_label 操作（卷标写入已并入格式化） |
| ⬜ 待办 | 多 DPI 档位验收、CSV 导出、多盘排队执行、失败策略（stop/continue/retry）、WM_DEVICECHANGE 事件驱动、GitHub 远端托管（工作流已就绪待生效） |

---

## 4. 功能规格

### 4.1 设备层
- **插拔检测**：1 秒轮询（`GetLogicalDrives` + `GetDriveType == DRIVE_REMOVABLE`），只负责发现设备，**不做机型推断**；`WM_DEVICECHANGE` 事件驱动列为后续升级项
- **设备信息**：盘符、卷标、文件系统、容量、卷序列号；另经 SetupAPI + CfgMgr 父设备链解析 **真实 USB 设备节点**的 VID/PID 与 USBSTOR 序列号（注意取 USB 设备本身而不是上级 Hub；用于区分同型号个体与 USB 速度匹配），内置盘正确回退为空
- **机型匹配**：机型为独立状态——启动取 ini 的 `DEFAULTMODEL`，运行中操作员可随时下拉切换（切换即重载配置）；插盘一律使用"当前选中机型"配置节执行

### 4.2 整任务编排（task_thread，自动/手动共用）

触发后：**自动触发使用刚插入的盘符**；手动按钮/热键取第一个可移动盘符（无盘报错）。`g_running` 互斥（执行中新请求忽略），串行执行：

1. **USB 识别** op_usbinfo —— `ENABLE_USBINFO=0` → 按配置跳过（`OP_SKIP_CFG`，成功类别）
2. **复制** op_write —— 手动触发总是执行；自动触发须 `AUTO_COPY=1`，否则按配置跳过（成功类别）
3. **读取判定** op_read —— `ENABLE_READ=0` 或未配 `READFILE` → 按配置跳过（成功类别；自动/手动均执行）
4. **读写测试** op_rwtest —— 手动触发总是执行；自动触发须 `AUTO_TEST=1`，否则按配置跳过（成功类别）

每步执行前复核目标盘身份（卷序列号/VID/SN，变化即中止）；每步之间及复制过程中检查停止标志（[停止] 按钮/停止监控，`CopyFileExW` 的 `pbCancel` 可立即取消复制）。

**结果码与聚合（v1.0.0 第 2 批修复）**：`op_*` 统一返回 `OP_OK=0 / OP_FAIL=1 / OP_STOPPED=2 / OP_SKIP_CFG=3`。任务不再用 `rc |=` 组合，改为“执行步数 / 按配置跳过步数 / 失败标志 / 停止标志”：失败优先 → `OP_FAIL`；否则停止 → `OP_STOPPED`；否则全部按配置跳过 → `OP_SKIP_CFG`；否则 `OP_OK`。结束日志形如 `=== 任务结束（成功）：执行 N 步，按配置跳过 M 步 ===`。
判定政策：**按配置正常屏蔽的步骤跳过不改变成功结论**；**程序自行降级/未实现/无法校验（PROCESSED、REGEX、未知 READRULE、非 md5、MD5 无法计算、读取中换盘）一律 `OP_FAIL`，不得报成功**。退出程序时先请求停止并等待工作线程结束（最长 30s）。

**插拔事件与待处理盘（第 2 批修复）**：轮询→GUI 的消息改为“变化盘符位图 + 插入/拔出”，同一次轮询先报拔出、后报插入（不再丢同周期换盘事件）；任务执行中插入的盘不会清空当前输出，只记为“待处理盘”，当前任务**成功/按配置跳过**结束后若仍处于自动模式则补执行，失败/停止则不自动重试；待处理盘拔出会取消。

> **三层职责分离（易混淆点）**：**ENABLE_*** 只决定"步骤/按钮是否存在"；**监控开关 + AUTORUN** 只决定"插盘要不要自动跑"（1104 的 AUTO/MANUAL 唯一判定）；**AUTO_COPY / AUTO_TEST** 只决定"自动流程内做不做复制/测试"。全流程条件判断详见《插拔全流程图.html》。

### 4.3 操作引擎（ops.c，`op_*` 函数族）

| 操作 | 实装状态 | 说明 |
|---|---|---|
| `op_write`（复制） | ✅ | `WRITEMODE=COPY`：按 `SRCPATH`（目录+通配文件名，相对路径以 exe 目录为基准）复制到 `DSTPATH`（自动建多级目录，同名覆盖）；`CopyFileExW` 回调按字节推进进度条并支持 `pbCancel`；`HASH=md5`（仅支持 md5，其它值直接判失败）逐文件源/目标 MD5 比对：**不一致自动删除目标并重试复制一次，仍不一致删除坏副本并判失败；MD5 无法计算也判失败**；`ENABLE_WRITE=1` 但未配 `SRCPATH` 判失败；`PROCESSED` 未实现，配置后判失败（不得按成功）；复制中设备丢失（错误码 21/1167/1112）立即中止 |
| `op_read`（读取判定） | ✅ | `READFILE` 支持子目录前缀与通配符（如 `SYSTEM\PAW6K-LSN`、`GTB907785*.txt`），`ENABLE_READ=0`/留空 → 按配置跳过（成功类别）；`MATCH_POLICY`：NEWEST（最新时间，默认）/ FIRST（首个）/ STRICT（多命中即 FAIL 并列出全部）；`READRULE` 规则链（`MIN_LEN:n`、`MUST_CONTAIN:k`，`;` 分隔）判定，**留空=按配置不做内容规则；出现未识别规则名判失败**；`READPATTERN=REGEX` 未实现→判失败（不再降级 WILDCARD）；读取前后复核设备身份，读中换盘→判定作废并判失败；PASS → 左窗格显示内容 + 绿 PASS；FAIL → 红徽标 + 失败原因；第二命中文件投右窗格 |
| `op_format`（格式化） | ✅ | fmifs.dll `FormatEx` 快速格式化，`FILESYSTEM`（FAT32/EXFAT）+ `VOLUME` 卷标回写；`ENABLE_FORMAT=0` → 按配置跳过（成功类别）；**格式化前强制人工确认**（`CONFIRM` 仅兼容保留，置 0 不跳过），确认框显示盘符/卷标/容量/卷序列号/VID/SN；确认前先校验 `FILESYSTEM`（仅 FAT32/EXFAT）与 `VOLUME`（≤11 字符、禁 `\ / : * ? " < > |` 与控制字符）；确认后复核设备身份，变化则取消（不调用 FormatEx，取消返回 `OP_STOPPED=2`）；格式化成功后写卷标（失败判 FAIL）、刷新设备信息与界面（`WM_APP_DEVREFRESH`）；进度条随 fmifs 回调推进 |
| `op_usbinfo` | ✅ | USB 通讯模式识别（非测速）：先经 SetupAPI 枚举 USB Hub（`GUID_DEVINTERFACE_USB_HUB`），对每个端口用 `IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX` 按 `VID/PID` 匹配目标设备，取链路速度（Low/Full/High/Super，`_EX_V2` 区分 5G/10G）；同 VID/PID 多设备/无权限/未匹配时标注“未知（原因）”不报错；Type-C 正反插为预留提示 |
| `op_label_read` | ✅ | 读取卷标/文件系统/序列号/容量/可用空间，刷新标签行并写日志；盘已移除/不可访问或盘未格式化（取不到卷信息）时判失败，不再对异常盘报成功 |
| `op_rwtest` | ✅ | 写 8MB 随机数据 → 回读 → MD5 比对 + 读写速率；优先 `FILE_FLAG_NO_BUFFERING|FILE_FLAG_WRITE_THROUGH` + 扇区对齐缓冲直读介质（避免系统缓存导致坏块假阴性），不支持时回退缓冲并告警；测试文件带 PID、用完删除；不一致报“可能存在坏块” |
| `op_gen_ident` | ⏸ 预留 | 按台标识生成扩展；当前无入口调用、不参与任务，CMD-B 输出区预留（生成规则待定） |

**通配符匹配**：`*` 任意长度、`?` 单字符（ASCII 大小写不敏感，与 Windows 文件名语义一致）；命中的可变片段捕获为 `${MATCH_1}` 写日志（模板替换复用为预留项）；`READPATTERN=REGEX` 为预留，配置后读取步骤判失败（不得静默降级 WILDCARD）。

### 4.4 配置文件（ini，多编码自动识别）

```ini
[TOOLSINFO]
TITLE=U盘拷盘工具               ; 窗口标题
VERSION=1.0.0                  ; 配置文件版本（≠软件版本；软件版本由 src/version.h 固定，当前 1.2.0）
ADMINPASS=1234                 ; 配置窗口管理员密码（ADMINPASS_HASH 为预留项）
PRJNUM=4
PRJNAME1=GENERIC               ; 内部键：与节名一一对应，程序结构依赖，永不改动
DISPLAYNAME1=通用机型          ; 显示名：界面/下拉/配置窗口显示，可随时改
DEFAULTMODEL=GENERIC           ; 启动时默认选中的机型（内部键）

[GENERIC]
; ---- 功能开关（每机型独立，0=按钮置灰/自动流程跳过并记日志）----
ENABLE_READ=1    ENABLE_WRITE=1    ENABLE_FORMAT=1    ENABLE_USBINFO=1
; ---- 自动策略 ----
AUTORUN=0                       ; 1=插盘即自动执行整任务（无需开监控）
AUTO_COPY=1                     ; 自动流程内是否复制（手动点〔复制〕不受限）
AUTO_TEST=0                     ; 自动流程内是否读写测试
; ---- 按钮显示名（可省略，缺省用默认名）----
BTN_WRITELABEL=升级             ; 如通用机型把〔复制〕按钮显示为"升级"
; ---- 写入 ----
WRITEMODE=COPY                  ; COPY=简单复制 | PROCESSED=按台生成（预留）
SRCPATH=.\upgrade_GENERIC\*.*   ; 复制源（目录+通配，相对路径以 exe 目录为基准）
DSTPATH=\upgrade\               ; 复制目标（U 盘内）
; ---- 读取 ----
READFILE=SYSTEM\PAW6K-LSN       ; 读取文件：固定名/通配符/子目录前缀
MATCH_POLICY=NEWEST             ; NEWEST / FIRST / STRICT
READPATTERN=WILDCARD            ; WILDCARD | REGEX(预留)
READRULE=MIN_LEN:1              ; 判定规则链，; 分隔（MUST_CONTAIN:关键字）；; 前不要加空格
; ---- 格式化 ----
CONFIRM=1                       ; 兼容保留：格式化前始终强制确认
FILESYSTEM=FAT32                ; FAT32 | EXFAT
VOLUME=                         ; 卷标（格式化成功后写入）
HASH=md5                        ; 复制后校验算法
```

**编码与解析（第 4 批修复）**：统一 `text_decode` 探测 UTF-8±BOM / UTF-16LE/BE±BOM / 无 BOM UTF-16（零字节启发式）/ GBK；损坏 UTF-8（少量非法字节）与二进制**拒绝并写具体原因**（不再静默按 GBK 读出乱码）；容量超限（>16 机型/>48 键/键名 64/值 512）、截断、畸形节头（缺 `]`）、空节名、超长节名、重复节（合并）、PRJNAME 不匹配全部写 WARN；读取含共享 DELETE。**内嵌出厂配置 `src/default_ini.h` 由 `profiles/UdiskTools.ini` 生成，CI 用 `scripts/gen_default_ini.ps1 -Check` 校验两者一致（防漂移）**；工区中 `profiles/UdiskTools.ini` 与 `src/default_ini.h` 必须同批提交。

实际参考配置为 4 种机型（见 `profiles\UdiskTools.ini`）：
GENERIC=通用机型 / WT-2=WT-2机型 / N2010=N2010机型 / K2311-S3=K2311(S3)机型。
键值编辑方式全键对照见《配置文件使用手册.md》8.1 节。

### 4.5 界面（分区概览）

窗口标题 `<TITLE> v<配置版本>（软件 v<软件版本>）`（配置版本为空时省略 `v<配置版本>`）；不做托盘常驻，关闭即退出（任务运行中先确认）。

主窗口纵向分区：**顶部**（机型下拉 1001、开始/停止 1002/1003、格式化 1004、读取标签 1005、复制 1006、配置 1007、置顶 1008、AUTO_COPY/AUTO_TEST 灰只读状态框 1009/1018、快捷键使能 1013）→ **状态行**（标签行 1101、AUTO/MANUAL 1104、SRCPATH 1105、READRULE 规则标签 1109、插拔计数器 1017、监控状态/命中行 1102/1103/1108）→ **设备区**（设备状态 1201、出错徽标 1202）→ **任务区**（进度条 1301 + 前置提示 1106 + 尾部百分比 1107）→ **控制台**（CMD-A 操作日志 1401；CMD-B 输出区 1402 预留）→ **文件区**（判定徽标 1501、双内容窗格 1502/1503）→ **底部监控窗格**（开始/停止监控 1010/1011、退出 1012、清屏 1014、读取校验 1015、读写测试 1016）。全局快捷键 Ctrl+Alt+T=读写测试 / Ctrl+Alt+C=复制（2001/2002）。

- **逐控件 ID、类型、状态与刷新规则（53 个 ID 全覆盖）以《UI功能状态说明.md》为准**（权威文档，本文不再重复逐控件表格）；控件位置见《控件位置对照图.html》。
- **出错标记联动**：1202 徽标点亮（红）+ CMD 区 `[ERR]` 行红色 + 窗口标题追加 `[!]`；下次插盘或成功后熄灭，错误保留在日志。
- **配置窗口 `WND_CONFIG`**：默认只读，[解锁编辑] 3003 校验 `ADMINPASS`（密码错误只提示并写日志，**可无限重试、不做次数锁定**——用户明确要求）；键名下拉 3011（支持手输）+ 枚举键走值下拉 3012、自由键走输入框 3009（标签 3014 随类型切换）；[保存] 3004 / [恢复默认设置] 3010 仅解锁后可用，均检查重载结果（失败不假报成功），恢复默认先备份 `.bak` + PID 临时文件原子写 + 回读校验；解锁/保存/恢复均写日志（不含密码内容）。逐控件细节见《UI功能状态说明.md》§3。

### 4.6 日志（强制，不可关闭）
- `exe_dir\logs\YYYYMMDD.log` 按日期滚动：时间、通道、级别、内容；**日志句柄按天缓存复用**（不再每行 CreateFile/Close）、共享 `READ|WRITE|DELETE`、单文件 >8MB 自动轮转 `.log.1`、主文件被占用时写 `_alt.log` 不丢行、`log_shutdown()` 安全收尾
- CMD-A 与文件日志同步输出（applog 双通道；CMD-B 为预留输出区，当前无内容）；文件写失败不阻塞 GUI 消息（OutputDebugString 兜底）
- CSV 导出为待办项
- **崩溃转储**：`SetUnhandledExceptionFilter` + `MiniDumpWriteDump`（dbghelp）→ `logs\crash_YYYYMMDD_HHMMSS.dmp`，日志记录异常码/地址/模块，不吞异常

### 4.6.1 命令行与单实例

- 单实例：`Local\UdiskTools.SingleInstance` 互斥量；第二实例 `FindWindowW(UDK_MAIN_WNDCLASS)` → 激活现有窗口后 exit 0（不弹窗）。
- CLI：`--ini <path>`（引号/相对路径）、`--check-ini`（不建 GUI，0/1，可用于 CI/排查）、`--version/-v`、`--help/-h/-?`；优先 `AttachConsole(ATTACH_PARENT_PROCESS)` 输出，失败再 MessageBox。
- 退出码：0 正常；1 消息循环/初始化失败；3 关窗等待线程超时强制退出；CLI `--check-ini` 0/1。

### 4.7 DPI 适配
1. 启动时运行态声明 Per-Monitor V2（`SetProcessDpiAwarenessContext`，失败回退 Per-Monitor/system aware）
2. 所有窗口/控件/字体尺寸经 `GetDpiForWindow()` 换算（`dpi_scale` 宏），禁止写死像素
3. 处理 `WM_DPICHANGED`：重建字体、`SetWindowPos` 重排
4. 多档位（100%–200%）验收为待办项

---

## 5. 技术方案

| 项 | 选择 |
|---|---|
| 语言 | C11，纯 Win32 API（无第三方 GUI 框架） |
| 编译 | MinGW-w64（gcc / llvm-mingw），Makefile（`-municode -mwindows`） |
| 字符集 | 全程 Unicode（W 系列 API），ini 兼容读取 |
| 哈希 | 自带 md5.c（公共域实现） |
| 格式化 | fmifs.dll `FormatEx`（format.com 回退未实现） |
| 设备链 | SetupAPI + CfgMgr32（VID/PID/序列号）；USB Hub IOCTL 速度识别（usbspeed.c） |
| 热键 | RegisterHotKey |
| 并发 | CreateThread + Interlocked 互斥（`g_running`/`g_stop`）：工作线程执行 IO，UI 线程不卡；**任务/单操作启动时在 UI 线程按值快照当前机型配置（`TASKARG`/`OPARG` 内嵌 `CFGMODEL`），worker 只读快照**，运行中切换机型/保存配置不再造成数据竞争 |
| CI | GitHub Actions `.github/workflows/build.yml`（push/PR 触发 make 构建 + 冒烟测试 + **默认配置一致性门禁** `gen_default_ini.ps1 -Check`，待远端托管生效；本地 `mingw32-make check-default-ini` 等价） |
| 资源/诊断 | windres `.rc`（VERSIONINFO/ICON/RT_MANIFEST）+ dbghelp MiniDumpWriteDump；manifest asInvoker+comctl32 v6+PerMonitorV2+longPathAware |
| 交付 | 单 exe（数百 KB 量级，含图标/版本/manifest），免安装免运行库；CLI 可脚本化（--check-ini 等） |

**实际代码结构**（原规划的 ui.c、ini.c、rule.c、log.c 独立文件未采用：GUI 分为 gui.c（主窗口）与 gui_config.c（配置窗口），ini 归 config.c，规则链内置于 ops.c，日志归 util.c）：
```
udisktools/
├── src/
│   ├── main.c          # WinMain、消息循环、ini 缺失启动交互（内嵌默认配置）
│   ├── gui.c           # WND_MAIN 主窗口：控件、布局、DPI、任务编排
│   ├── gui_config.c    # WND_CONFIG 配置窗口：机型编辑、锁定态、保存/恢复默认
│   ├── gui_shared.h    # gui.c / gui_config.c 内部共享符号声明
│   ├── ops.c           # op_write/op_read/op_format/op_usbinfo/op_label_read/op_rwtest/op_gen_ident
│   ├── device.c/h      # 驱动器枚举、卷信息、VID/PID/SN、插拔轮询
│   ├── config.c/h      # ini 解析写回（保留注释）、机型双层映射
│   ├── pattern.c/h     # 通配符匹配、可变片段捕获
│   ├── util.c/h        # applog 双通道日志、路径、编码、哈希封装
│   ├── usbspeed.c/h    # USB Hub IOCTL 通讯模式（链路速度）匹配
│   └── md5.c/h
├── ui/resource.h       # 全部控件与窗口/图标 ID 登记
├── res/                # .rc / manifest / ico（版本信息、图标、DPI/控件清单）
├── profiles/           # 参考配置（4 机型）
├── docs/               # 文档区（扁平结构：索引 README / CHANGELOG / 手册 / UI状态说明 / 本文件 / 开发流程 / 流程图 / 对照图 / 审计报告 + archive/）
├── scripts/            # default_ini.h 生成器、跨进程回归脚本
├── .github/workflows/  # CI（make 构建 + 默认配置门禁 + 冒烟测试）
├── tests/              # 单元测试占位（Phase 3）
└── Makefile / CMakeLists.txt
```

---

## 6. 开发计划（Phase 制，不含时间安排）

### Phase 1 — 引擎与设备层 ✅ 已完成
- ini 解析写回（保留注释）、机型双层映射、通配符匹配、READRULE 规则链
- 设备枚举与卷信息、VID/PID/SN 设备链解析
- write（COPY）、read+判定（含子目录）、rwtest、format、usbinfo、label_read

### Phase 2 — GUI 集成 ✅ 已完成
- WND_MAIN 全部分区与控件、CMD-A 日志输出区（CMD-B 输出区预留）、AUTO/MANUAL 与出错标记、监控窗格与插拔计数
- 插盘自动触发（监控/AUTORUN）、快捷键 Ctrl+Alt+T/C、CONFIRM 确认
- WND_CONFIG（只读+密码解锁+键值编辑+恢复默认）、置顶、清屏
- 双文件内容窗格、按字节进度条、按钮显示名机型覆盖
- 版本迭代至软件 v1.2.0（alpha.N → v1.0.0 → v1.2.0，每轮 feature→dev→main + tag）

### Phase 3 — 增强与发布 ⏳ 进行中
剩余事项（按优先级）：
- [ ] 多 DPI 档位（100%–200%）验收
- [ ] 异常处理补强：拔盘中断全路径、磁盘满、只读盘、FAT32 4GB 限制、劣质盘超时
- [ ] `gen_serial`/`gen_mac` 按台标识生成规则落地（启用 CMD-B 预留输出区；当前无入口）
- [ ] WRITEMODE=PROCESSED 模板按台生成（`${SN}`/`${MAC}`/`${DATE}` 占位符）
- [ ] READPATTERN=REGEX、ADMINPASS_HASH、CSV 导出
- [ ] 多盘排队执行、失败策略（stop/continue/retry）、WM_DEVICECHANGE 升级、设备白名单
- [ ] 打包发布：单 exe + 示例 ini + 使用说明 + GitHub 远端托管（CI 工作流已就绪）

**M3 里程碑**：v1.0 正式版发布；软件 v1.2.0 为界面可用性优化版。

---

## 7. 开发流程（要点）

构建、测试、CI 门禁、提交规范、分支模型与版本发布的**完整规则见《开发流程.md》**（权威文档，本文不再重复）。要点：

- **分支**：`feature/<主题>` → `dev` → `main`（合流后删 feature 分支）；远端托管后 push → GitHub Actions CI（build.yml 已就绪）→ PR 合入 dev
- **提交**：Conventional Commits，一条提交只做一件事，说明"为什么"
- **版本**：软件版本（`src/version.h` + `res/udisktools.rc`，当前 1.2.0）与配置文件版本（ini `[TOOLSINFO] VERSION`，当前 1.0.0）**相互独立**；发版 tag 用软件版本
- **质量门禁**：编译零警告、默认配置一致性校验（`gen_default_ini.ps1 -Check`）、`tests/run_tests.ps1` 全过、CLI 冒烟（`--version`/`--check-ini`）、文档同步（维护规则见 `docs/README.md` §3）
- **本地环境**：MinGW-w64 工具链 ✅、Makefile 构建入口 ✅（cmake 备用）；真盘验证 U 盘（历史验证盘符 F:）；旧软件参考件 `D:\workbuddy\WR_Udisk\temp\`（DiskRW_V2.0 / read-U / K2311 / K2411）

---

## 8. 验收标准（摘要）
1. 插入 U 盘（监控开启或 AUTORUN=1）数秒内按当前选中机型自动执行；MANUAL 模式按钮/热键立即响应
2. READRULE 判定 PASS 才显示内容；FAIL 触发出错标记三处联动
3. 复制后 MD5 比对 100% 一致；进度条按字节实时推进
4. 通配符读取：换盘（可变尾号变化）后仍能正确命中；子目录前缀（SYSTEM\PAW6K-LSN）正常
5. 配置窗口默认只读，密码解锁后可编辑（密码错误可重试，不做次数锁定）
6. ENABLE_* 禁用的操作：按钮置灰、自动跳过、日志记录
7. 每次任务可查日志（含版本号）；GUI 在 100%–200% 四档 DPI 下无虚化（待验收）
8. 任务中拔盘：任务终止并提示，软件不崩溃，可继续下一次任务
9. 窗口标题同时显示配置版本与软件版本：`<TITLE> v<配置版本>（软件 v1.2.0）`；配置版本为空时不显示 `v<...>`；`--version` 显示软件版本 1.2.0，`--check-ini` 显示配置文件版本
10. 主窗口 AUTO_COPY/AUTO_TEST 灰只读状态框与当前机型配置一致且不可编辑；`AUTO_TEST=1` 时 READRULE 标签显示规则（空规则显示“未配置”），`AUTO_TEST=0` 时为空

---

## 9. 风险与对策

| 风险 | 对策 |
|---|---|
| exFAT 格式化部分系统需提权 | 不做全局管理员要求；失败时明确提示并引导按需提权 |
| 轮询插拔有延迟/漏检 | 1s 轮询成熟可靠；WM_DEVICECHANGE 升级列为 Phase 3 待办 |
| USB 模式识别依赖硬件/驱动 | 协议查询失败标注"未知"不报错 |
| 通配符/长路径/中文编码 | util.c 集中处理，全程宽字符 |
| 劣质 U 盘卡死/掉盘 | 复制/测试循环内 CHECK_STOP 可中止；读写测试暴露坏块；超时强制作废列为待办 |
| Win32 通知时序类缺陷（如 CBN_SELCHANGE 编辑框滞后） | 关键交互以列表项/消息参数为准，不依赖滞后文本；回归脚本固化 |

---

## 10. 已确认决议（含迭代中新增）
- ✅ 不强制管理员权限（仅格式化按需提权）
- ✅ 日志强制输出，不可关闭；不做托盘常驻
- ✅ 不做精准测速，改为 USB 通讯模式识别（usbinfo）
- ✅ 完全重构，旧软件仅作功能模式参考，不做迁移与向后兼容
- ✅ 机型内部键与显示名双层映射（实际 4 机型：GENERIC / WT-2 / N2010 / K2311-S3）
- ✅ 机型匹配与轮询解耦（DEFAULTMODEL / 下拉随时改，切换即重载配置）
- ✅ CMD-A 日志输出区（CMD-B 输出区预留）+ 全控件 ID 登记 + AUTO/MANUAL 与出错状态标记
- ✅ 每机型 ENABLE_* 功能开关；读取先判定再显示；复制按字节进度 + MD5 校验
- ✅ 自动策略三层分离：ENABLE_*（使能）/ 监控+AUTORUN（自动与否）/ AUTO_COPY·AUTO_TEST（自动流程内裁剪）
- ✅ 读取支持子目录前缀与通配符（REGEX 预留）；MATCH_POLICY 三策略
- ✅ 插拔计数器（仅监控开启时计数，4 位循环，退出清零）
- ✅ 配置窗口：键名下拉（支持手输）+ 枚举/自由键值控件自动切换 + 恢复默认设置
- ✅ 双文件内容窗格（第一/第二命中）+ 左右专属提示行
- ✅ 窗口置顶、Ctrl+Alt+T/C 全局快捷键（使能复选框控制；单操作语义）
- ✅ 迭代发布：v1.0.0-alpha.N tag 序列 → 软件 v1.2.0（功能优化）
- ✅ DPI Per-Monitor V2 + WM_DPICHANGED 动态重排
- ✅ CI 工作流 build.yml 就绪（GitHub Actions）

## 11. 待确认事项
- [ ] `gen_serial` / `gen_mac` 生成规则：流水号段、MAC 池来源、防重复持久化
- [ ] PROCESSED 模板路径与占位符规范（`${SN}`/`${MAC}`/`${DATE}`/`${MATCH_1}`）
- [ ] CSV 导出字段（rwtest 结论、usbinfo 通讯模式）
- [ ] GitHub 远端仓库接入时机（CI 随远端生效）
