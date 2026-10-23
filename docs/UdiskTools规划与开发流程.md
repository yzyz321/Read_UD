# UdiskTools 规划书与开发流程（合并版）

> 版本：v1.1 ｜ 日期：2026-10-23 ｜ 状态：按 **v1.0.0-alpha.19** 实际实现修订
> 本文件由原《UdiskTools软件规划书.md》与《开发流程.md》合并而成，以代码实际行为准；
> 与本文冲突的旧描述一律作废。
> 定位：Windows 可移动磁盘（U 盘）自动化操作工具 · C 语言全新开发

---

## 1. 项目概述

### 1.1 定位
**UdiskTools** 是一款 Windows 单文件 GUI 工具（纯 Win32 API，无第三方运行时依赖）：一个 exe + 一个 ini。通过配置文件定义每种机型的操作流程，支持**自动**（插盘触发）与**手动**（按钮/热键）两种执行方式，覆盖复制写入、读取判定、格式化、USB 通讯模式识别、标签读取、读写测试等能力，全程强制留痕（双 CMD 输出区 + 日志文件）。

### 1.2 核心设计原则
| 原则 | 说明 |
|---|---|
| 配置驱动 | 操作内容、功能开关、判定规则全部来自 ini，改配置不改代码 |
| 双层机型命名 | 内部键（程序结构依赖，永不改）与显示名（操作员可见，随时改）分离 |
| 权限最小化 | 日常操作普通用户权限即可；仅格式化按需提权 |
| 强制留痕 | 所有操作写日志，不可关闭；界面 CMD 区与文件日志双通道 |
| 高 DPI 优先 | Per-Monitor V2 感知 + `WM_DPICHANGED` 动态重排 |

### 1.3 运行环境
- Windows 10 / 11 x64
- 编译工具链：**MinGW-w64（gcc / llvm-mingw 均可）**，Makefile 构建（CMakeLists.txt 备用）
- 交付物：单 exe（免安装、免运行库）+ 示例 ini + 使用说明

---

## 2. 实施状态总览（截至 v1.0.0-alpha.19）

主体功能已全部实装并通过真盘验证（READ 子目录命中、复制进度、配置编辑等），
版本按 `v1.0.0-alpha.N` 迭代发布（当前 19 个 tag）。

| 状态 | 内容 |
|---|---|
| ✅ 已实装 | 插拔轮询与设备信息（含 VID/PID/SN）、整任务编排（识别→复制→读取判定→测试）、复制（按字节进度+MD5 校验）、读取判定（子目录/通配/策略/规则链/双窗格）、格式化（fmifs + 确认 + 卷标）、USB 速度识别、读取标签、读写测试、配置窗口（下拉/输入/解锁/恢复默认）、双 CMD、监控窗格与插拔计数、热键、置顶、DPI 动态重排 |
| ⏸ 预留（v1.x 后续） | WRITEMODE=PROCESSED、READPATTERN=REGEX、Type-C 正反插识别、gen_ident 按台标识生成（现为 CMD-B 演示输出）、ADMINPASS_HASH、独立 verify 操作（MD5 已内置于复制）、独立 write_label 操作（卷标写入已并入格式化） |
| ⬜ 待办 | 多 DPI 档位验收、CSV 导出、多盘排队执行、失败策略（stop/continue/retry）、WM_DEVICECHANGE 事件驱动、GitHub 远端托管 |

---

## 3. 功能规格

### 3.1 设备层
- **F1.1 插拔检测**：1 秒轮询（`GetLogicalDrives` + `GetDriveType == DRIVE_REMOVABLE`），只负责发现设备，**不做机型推断**；`WM_DEVICECHANGE` 事件驱动列为后续升级项
- **F1.2 设备信息**：盘符、卷标、文件系统、容量、卷序列号；另经 SetupAPI + CfgMgr 父设备链（Disk→USBSTOR→USB）解析 **VID/PID 与 USBSTOR 序列号**，用于区分同型号的不同个体（内置盘正确回退为空）
- **F1.3 机型匹配**：机型为独立状态——启动取 ini 的 `DEFAULTMODEL`，运行中操作员可随时下拉切换（切换即重载配置）；插盘一律使用"当前选中机型"配置节执行

### 3.2 整任务编排（task_thread，自动/手动共用）

触发后取**第一个可移动盘符**（无盘报错），`g_running` 互斥（执行中新请求忽略），串行执行：

1. **USB 识别** op_usbinfo —— `ENABLE_USBINFO=0` 跳过
2. **复制** op_write —— 手动触发总是执行；自动触发须 `AUTO_COPY=1`，否则记日志跳过
3. **读取判定** op_read —— `ENABLE_READ=0` 或未配 `READFILE` 跳过（自动/手动均执行，核心校验）
4. **读写测试** op_rwtest —— 手动触发总是执行；自动触发须 `AUTO_TEST=1`

每步间检查停止标志（[停止] 按钮/停止监控），结束时按 rc 汇总：成功=绿"任务完成"、已停止=绿"任务已停止"、有错误=红徽标（由各 op 内部置位）。

> 三层职责分离（易混淆点）：**ENABLE_*** 只决定"步骤/按钮是否存在"；**监控开关 + AUTORUN** 只决定"插盘要不要自动跑"（1104 的 AUTO/MANUAL 唯一判定）；**AUTO_COPY / AUTO_TEST** 只决定"自动流程内做不做复制/测试"。详见《插拔全流程图.html》。

### 3.3 操作引擎（ops.c，`op_*` 函数族）

| 操作 | 实装状态 | 说明 |
|---|---|---|
| `op_write`（复制） | ✅ | `WRITEMODE=COPY`：按 `SRCPATH`（目录+通配文件名，相对路径以 exe 目录为基准）复制到 `DSTPATH`（自动建多级目录，同名覆盖）；`CopyFileExW` 回调按字节推进进度条；`HASH=md5` 时逐文件源/目标 MD5 比对；`PROCESSED` 模式为预留（记日志跳过） |
| `op_read`（读取判定） | ✅ | `READFILE` 支持子目录前缀与通配符（如 `SYSTEM\PAW6K-LSN`、`GTB907785*.txt`）；`MATCH_POLICY`：NEWEST（最新时间，默认）/ FIRST（首个）/ STRICT（多命中即 FAIL 并列出全部）；内容经 `READRULE` 规则链（`MIN_LEN:n`、`MUST_CONTAIN:k`，`;` 分隔）判定：PASS → 左窗格显示内容 + 绿 PASS；FAIL → 红徽标 + 失败原因；第二命中文件投右窗格 |
| `op_format`（格式化） | ✅ | fmifs.dll `FormatEx` 快速格式化，`FILESYSTEM`（FAT32/EXFAT）+ `VOLUME` 卷标回写；`CONFIRM=1` 弹窗二次确认（取消返回 rc=2）；进度条随 fmifs 回调推进 |
| `op_usbinfo` | ✅ | USB 通讯模式识别（非测速）：`IOCTL_STORAGE_QUERY_PROPERTY` 查询协议规格得 USB 2.0/3.x 速度；取不到标注"未知"不报错；Type-C 正反插为预留提示 |
| `op_label_read` | ✅ | 读取卷标/文件系统/序列号/容量/可用空间，刷新标签行并写日志 |
| `op_rwtest` | ✅ | 写 8MB 随机数据 → 回读 → MD5 比对 + 读写速率；不一致报"可能存在坏块" |
| `op_gen_ident` | ⏸ 演示 | 按台生成 SN/MAC（CMD-B 通道演示输出），生成规则待定（Phase 3） |

**通配符匹配**：`*` 任意长度、`?` 单字符；命中的可变片段捕获为 `${MATCH_1}` 写日志（模板替换复用为预留项）；`READPATTERN=REGEX` 为预留（暂按 WILDCARD 处理并记日志）。

### 3.4 配置文件（ini，UTF-8 with BOM）

```ini
[TOOLSINFO]
TITLE=U盘拷盘工具               ; 窗口标题
VERSION=1.0.0                  ; 软件版本（与 src/version.h 三处一致）
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
READRULE=MIN_LEN:1              ; 判定规则链，; 分隔（MUST_CONTAIN:关键字）
; ---- 格式化 ----
CONFIRM=1                       ; 格式化前弹窗确认
FILESYSTEM=FAT32                ; FAT32 | EXFAT
VOLUME=                         ; 卷标（格式化成功后写入）
HASH=md5                        ; 复制后校验算法
```

实际参考配置为 4 种机型（见 `profiles\UdiskTools.ini`）：
GENERIC=通用机型 / WT-2=WT-2机型 / N2010=N2010机型 / K2311-S3=K2311(S3)机型。

### 3.5 界面（Win32 原生控件，全部登记 `ui/resource.h`）

窗口标题 `TITLE vVERSION`；不做托盘常驻，关闭即退出（任务运行中先确认）。

| 区域 | 控件 ID | 说明 |
|---|---|---|
| 顶部 | `IDC_COMBO_MODEL` 1001 | 机型下拉框（DISPLAYNAME，启动取 DEFAULTMODEL，随时可改） |
| 顶部 | `IDC_BTN_START` 1002 / `IDC_BTN_STOP` 1003 | 整任务开始 / 停止（停止仅执行中可用） |
| 顶部 | `IDC_BTN_FORMAT` 1004 | 格式化（ENABLE_FORMAT） |
| 顶部 | `IDC_BTN_READLABEL` 1005 | 读取标签 |
| 顶部 | `IDC_BTN_WRITELABEL` 1006 | **复制按钮**（默认名"复制"，机型可覆盖显示名，如"升级"；执行 op_write，ENABLE_WRITE 控制） |
| 顶部 | `IDC_BTN_CONFIG` 1007 | 打开配置窗口 |
| 顶部 | `IDC_CHK_TOPMOST` 1008 | 窗口置顶复选框（HWND_TOPMOST/NOTOPMOST） |
| 顶部 | `IDC_CHK_HOTKEY` 1013 | 快捷键使能（勾选后注册 T/C 全局热键，均触发整任务） |
| 底部监控窗格 | `IDC_BTN_MONSTART` 1010 / `IDC_BTN_MONSTOP` 1011 / `IDC_BTN_EXIT` 1012 | 开始监控（绿）/ 停止监控（红）/ 退出（自绘变色按钮） |
| 底部监控窗格 | `IDC_BTN_CLEARCMD` 1014 | 清屏 CMD-A |
| 底部监控窗格 | `IDC_BTN_READCHECK` 1015 / `IDC_BTN_RWTEST` 1016 | 读取校验 / 读写测试（手动单操作） |
| 底部监控窗格 | `IDC_STC_INSERTCOUNT` 1017 | 插拔计数器"计数:0000"（仅监控开启时累加，4 位循环，退出不保存） |
| 状态行 | `IDC_STC_LABEL` 1101 | 标签行：卷标 + VID/PID/SN（插拔实时刷新） |
| 状态行 | `IDC_STC_AUTOMODE` 1104 | **AUTO（绿）/ MANUAL（蓝）**，唯一判定点=监控开启或 AUTORUN=1 |
| 状态行 | `IDC_STC_SRCPATH` 1105 | 当前机型 SRCPATH 显示行 |
| 状态行 | `IDC_STC_MONSTATUS` 1102 / `IDC_STC_MONFILE` 1103 / `IDC_STC_MONFILE2` 1108 | 监控状态行 / 左文件窗格提示 / 右文件窗格专属提示 |
| 设备区 | `IDC_STC_DEVICE` 1201 / `IDC_STC_ERROR` 1202 | 设备状态（盘符/卷标/容量）/ 出错徽标（红"✕ 出错"绿"✓ 正常"） |
| 任务区 | `IDC_PBAR_TASK` 1301 + `IDC_STC_PROGTITLE` 1106 + `IDC_STC_PROGPCT` 1107 | 进度条 + 前置提示 + 尾部百分比 |
| 控制台 | `IDC_EDIT_ENGINE` 1401（CMD-A）/ `IDC_EDIT_IDENT` 1402（CMD-B） | 操作引擎输出 / 标识生成输出（分级配色，只读） |
| 文件区 | `IDC_STC_READVERDICT` 1501 / `IDC_EDIT_FILE` 1502 / `IDC_EDIT_FILE2` 1503 | 判定徽标 PASS/FAIL / 内容左窗格（第一命中）/ 右窗格（第二命中） |
| 热键 | `ID_HOTKEY_T` 2001 / `ID_HOTKEY_C` 2002 | T/C 全局热键 = 触发整任务 |

**出错标记联动**：1202 徽标点亮（红）+ CMD 区 `[ERR]` 行红色 + 窗口标题追加 `[!]`；下次插盘或成功后熄灭，错误保留在日志。

**配置窗口 `WND_CONFIG`**（IDC_CFG_ 前缀）：
- 打开即显示全部配置，**默认只读**（状态 `IDC_STC_CFGSTATE` 3006）
- [解锁编辑] 3003 → 密码框 3002（掩码）比对 `ADMINPASS`：正确 → 可编辑；**连错 3 次锁定密码输入**并记日志
- 键名下拉 3011（列当前机型全部键，支持手输）→ 枚举键走值下拉 3012、自由键（SRCPATH/DSTPATH/READFILE 等）走输入框 3009，标签 3014 随类型切换"新值（下拉选择）/新值（输入）"；说明提示行 3013 显示键用途（未解锁时附锁定提示）
- [保存] 3004 写回 ini（保留注释与未涉及键）并立即重载配置刷新主界面；[恢复默认设置] 3010 用内嵌出厂 ini 覆盖（二次确认）
- 解锁/保存/恢复均写日志（不含密码）

### 3.6 日志（强制，不可关闭）
- `exe_dir\logs\YYYYMMDD.log` 按日期滚动：时间、通道、级别、内容
- CMD-A/B 与文件日志同步输出（applog 双通道）
- CSV 导出为待办项

### 3.7 DPI 适配
1. 启动时运行态声明 Per-Monitor V2（`SetProcessDpiAwarenessContext`，失败回退 Per-Monitor/system aware）
2. 所有窗口/控件/字体尺寸经 `GetDpiForWindow()` 换算（`dpi_scale` 宏），禁止写死像素
3. 处理 `WM_DPICHANGED`：重建字体、`SetWindowPos` 重排
4. 多档位（100%–200%）验收为待办项

---

## 4. 技术方案

| 项 | 选择 |
|---|---|
| 语言 | C11，纯 Win32 API（无第三方 GUI 框架） |
| 编译 | MinGW-w64（gcc / llvm-mingw），Makefile（`-municode -mwindows`） |
| 字符集 | 全程 Unicode（W 系列 API），ini 兼容读取 |
| 哈希 | 自带 md5.c（公共域实现） |
| 格式化 | fmifs.dll `FormatEx`（format.com 回退未实现） |
| 设备链 | SetupAPI + CfgMgr32（VID/PID/序列号） |
| 热键 | RegisterHotKey |
| 并发 | CreateThread + Interlocked 互斥（`g_running`/`g_stop`）：工作线程执行 IO，UI 线程不卡 |
| 交付 | 单 exe（数百 KB 量级），免安装免运行库 |

**实际代码结构**：
```
udisktools/
├── src/
│   ├── main.c          # WinMain、消息循环
│   ├── gui.c           # WND_MAIN/WND_CONFIG、控件、布局、DPI、任务编排
│   ├── ops.c           # op_write/op_read/op_format/op_usbinfo/op_label_read/op_rwtest/op_gen_ident
│   ├── device.c/h      # 驱动器枚举、卷信息、VID/PID/SN、插拔轮询
│   ├── config.c/h      # ini 解析写回（保留注释）、机型双层映射
│   ├── pattern.c/h     # 通配符匹配、可变片段捕获
│   ├── util.c/h        # applog 双通道日志、路径、编码、哈希封装
│   └── md5.c/h
├── ui/resource.h       # 全部控件与窗口 ID 登记
├── profiles/           # 参考配置（4 机型）
├── docs/               # 使用手册、插拔全流程图、本文件
├── scripts/            # default_ini.h 生成器、跨进程回归脚本
└── Makefile / CMakeLists.txt
```
（原规划的 ui.c、ini.c、rule.c、log.c 独立文件未采用：GUI 集中于 gui.c，ini 归 config.c，规则链内置于 ops.c，日志归 util.c。）

---

## 5. 开发计划（Phase 制，不含时间安排）

### Phase 1 — 引擎与设备层 ✅ 已完成
- ini 解析写回（保留注释）、机型双层映射、通配符匹配、READRULE 规则链
- 设备枚举与卷信息、VID/PID/SN 设备链解析
- write（COPY）、read+判定（含子目录）、rwtest、format、usbinfo、label_read

### Phase 2 — GUI 集成 ✅ 已完成
- WND_MAIN 全部分区与控件、双 CMD、AUTO/MANUAL 与出错标记、监控窗格与插拔计数
- 插盘自动触发（监控/AUTORUN）、热键 T/C、CONFIRM 确认
- WND_CONFIG（只读+密码解锁+键值编辑+恢复默认）、置顶、清屏
- 双文件内容窗格、按字节进度条、按钮显示名机型覆盖
- 版本迭代至 alpha.19（每轮 feature→dev→main + tag）

### Phase 3 — 增强与发布 ⏳ 进行中
剩余事项（按优先级）：
- [ ] 多 DPI 档位（100%–200%）验收
- [ ] 异常处理补强：拔盘中断全路径、磁盘满、只读盘、FAT32 4GB 限制、劣质盘超时
- [ ] `gen_serial`/`gen_mac` 按台标识生成规则落地（替换演示输出）
- [ ] WRITEMODE=PROCESSED 模板按台生成（`${SN}`/`${MAC}`/`${DATE}` 占位符）
- [ ] READPATTERN=REGEX、ADMINPASS_HASH、CSV 导出
- [ ] 多盘排队执行、失败策略（stop/continue/retry）、WM_DEVICECHANGE 升级
- [ ] 打包发布：单 exe + 示例 ini + 使用说明 + GitHub 远端托管

**M3 里程碑**：v1.0 正式版发布。

---

## 6. 开发流程

### 6.1 分支模型（当前实际执行方式）

| 分支 | 用途 |
|---|---|
| `main` | 稳定发布分支，始终可构建可运行，tag 打在此处 |
| `dev` | 集成开发分支，功能合入的集散地 |
| `feature/<主题>` | 单个功能开发（含 fix，如 `fix/cfg-key-selchange-sync`） |

迭代循环（本地执行，合流后删除 feature 分支）：
```
1. git switch main && git switch -c feature/<主题>
2. 开发（小步提交，每步保证可编译，gcc -Wall -Wextra 零警告）
3. 编译 + 控件回读/真盘验证自测
4. git switch dev && git merge --no-ff feature/<主题>
5. git switch main && git merge --no-ff dev
6. git tag v1.0.0-alpha.N && 删除 feature 分支
```
> 远端托管后升级为：push → CI 构建 → PR 合入 dev（当前 CI/PR 未启用，属 Phase 3 待办）。

### 6.2 提交规范（Conventional Commits）
```
feat: ini 解析器支持注释保留写回
fix: CBN_SELCHANGE 编辑框文本滞后导致值编辑器不切换
docs: 更新控件命名表
refactor: 提取 pattern.c 通配符匹配
```
一条提交只做一件事；提交信息说明"为什么"而不只是"改了什么"。

### 6.3 版本与发布
- 每轮功能/修复合入 main 后打 tag `v1.0.0-alpha.N`（N 递增）；正式发布打 `v1.0.0`
- 版本号与 `src/version.h`、ini 的 `VERSION` 键保持一致（发版前统一检查）

### 6.4 质量门禁
- 编译零 error；`-Wall -Wextra` 下不新增 warning
- 影响控件布局的改动必须对照 `ui/resource.h` 与 `docs/mockup/控件位置对照图.html`
- 涉及 ini 格式的改动必须同步：本文件、`docs/配置文件使用手册.md`、`profiles/UdiskTools.ini`、`scripts/gen_default_ini.py` 生成器
- 日志相关改动：确认"日志强制输出"红线未被破坏
- 修复类改动须附可复现的验证手段（跨进程控件回读脚本/真盘验证记录）

### 6.5 本地环境（已就绪）
- MinGW-w64 工具链 ✅、git ✅、Makefile 构建入口 ✅（cmake 备用）
- 真盘验证：可移动 U 盘（历史验证盘符 F:）

---

## 7. 验收标准（摘要）
1. 插入 U 盘（监控开启或 AUTORUN=1）数秒内按当前选中机型自动执行；MANUAL 模式按钮/热键立即响应
2. READRULE 判定 PASS 才显示内容；FAIL 触发出错标记三处联动
3. 复制后 MD5 比对 100% 一致；进度条按字节实时推进
4. 通配符读取：换盘（可变尾号变化）后仍能正确命中；子目录前缀（SYSTEM\PAW6K-LSN）正常
5. 配置窗口默认只读，密码解锁后可编辑，连错 3 次锁定
6. ENABLE_* 禁用的操作：按钮置灰、自动跳过、日志记录
7. 每次任务可查日志（含版本号）；GUI 在 100%–200% 四档 DPI 下无虚化（待验收）
8. 任务中拔盘：任务终止并提示，软件不崩溃，可继续下一次任务

---

## 8. 风险与对策

| 风险 | 对策 |
|---|---|
| exFAT 格式化部分系统需提权 | 不做全局管理员要求；失败时明确提示并引导按需提权 |
| 轮询插拔有延迟/漏检 | 1s 轮询成熟可靠；WM_DEVICECHANGE 升级列为 Phase 3 待办 |
| USB 模式识别依赖硬件/驱动 | 协议查询失败标注"未知"不报错 |
| 通配符/长路径/中文编码 | util.c 集中处理，全程宽字符 |
| 劣质 U 盘卡死/掉盘 | 复制/测试循环内 CHECK_STOP 可中止；读写测试暴露坏块；超时强制作废列为待办 |
| Win32 通知时序类缺陷（如 CBN_SELCHANGE 编辑框滞后） | 关键交互以列表项/消息参数为准，不依赖滞后文本；回归脚本固化 |

---

## 9. 已确认决议（含迭代中新增）
- ✅ 不强制管理员权限（仅格式化按需提权）
- ✅ 日志强制输出，不可关闭；不做托盘常驻
- ✅ 不做精准测速，改为 USB 通讯模式识别（usbinfo）
- ✅ 全新开发；机型内部键与显示名双层映射（实际 4 机型：GENERIC / WT-2 / N2010 / K2311-S3）
- ✅ 机型匹配与轮询解耦（DEFAULTMODEL / 下拉随时改，切换即重载配置）
- ✅ 双 CMD 显示区 + 全控件 ID 登记 + AUTO/MANUAL 与出错状态标记
- ✅ 每机型 ENABLE_* 功能开关；读取先判定再显示；复制按字节进度 + MD5 校验
- ✅ 自动策略三层分离：ENABLE_*（使能）/ 监控+AUTORUN（自动与否）/ AUTO_COPY·AUTO_TEST（自动流程内裁剪）
- ✅ 读取支持子目录前缀与通配符（REGEX 预留）；MATCH_POLICY 三策略
- ✅ 插拔计数器（仅监控开启时计数，4 位循环，退出清零）
- ✅ 配置窗口：键名下拉（支持手输）+ 枚举/自由键值控件自动切换 + 恢复默认设置
- ✅ 双文件内容窗格（第一/第二命中）+ 左右专属提示行
- ✅ 窗口置顶、T/C 全局热键（使能复选框控制）
- ✅ 迭代发布：v1.0.0-alpha.N tag 序列（当前 alpha.19）
- ✅ DPI Per-Monitor V2 + WM_DPICHANGED 动态重排

## 10. 待确认事项
- [ ] `gen_serial` / `gen_mac` 生成规则：流水号段、MAC 池来源、防重复持久化
- [ ] PROCESSED 模板路径与占位符规范（`${SN}`/`${MAC}`/`${DATE}`/`${MATCH_1}`）
- [ ] CSV 导出字段（rwtest 结论、usbinfo 通讯模式）
- [ ] GitHub 远端仓库与 CI 接入时机
