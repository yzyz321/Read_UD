# UdiskTools 软件规划书

> 版本：v1.0 ｜ 日期：2026-10-21 ｜ 状态：定稿，主体已实现（v1.0.0-alpha.1）
> 实施状态：核心代码完成并编译通过（config/device/pattern/ops/gui/util/md5 8 模块），
> 启动冒烟测试通过；待真盘验证各操作（read/write/format/rwtest）与多 DPI 档位验收。
> 定位：Windows 可移动磁盘（U 盘）自动化操作工具 · C 语言全新开发

---

## 1. 项目概述

### 1.1 定位
**UdiskTools** 是一款 Windows 单文件 GUI 工具（Win32 API，无第三方运行时依赖）：一个 exe + 一个 ini。通过配置文件定义每种机型的操作流程，支持**自动**（插盘触发）与**手动**（按钮/热键）两种执行方式，覆盖写入、读取判定、格式化、USB 通讯模式识别、标签读写、按台标识生成等能力，全程强制留痕（双 CMD 输出区 + 日志文件）。

### 1.2 核心设计原则
| 原则 | 说明 |
|---|---|
| 配置驱动 | 操作内容、功能开关、判定规则全部来自 ini，改配置不改代码 |
| 双层机型命名 | 内部键（程序结构依赖，永不改）与显示名（操作员可见，随时改）分离 |
| 权限最小化 | 日常操作普通用户权限即可；仅格式化按需提权 |
| 强制留痕 | 所有操作写日志，不可关闭；界面 CMD 区与文件日志双通道 |
| 高 DPI 优先 | Per-Monitor V2 感知从第一行代码做对，禁止后补 |

### 1.3 运行环境
- Windows 10 / 11 x64
- 编译工具链：**MinGW-w64（gcc）**，CMakeLists.txt / Makefile 构建
- 交付物：单 exe（免安装、免运行库）+ 示例 ini + 使用说明

---

## 2. 功能规格

### 2.1 设备层
- **F1.1 插拔检测**：1 秒轮询（`GetLogicalDrives` + `GetDriveType == DRIVE_REMOVABLE`），只负责发现设备，**不做机型推断**；Phase 3 可选升级 `WM_DEVICECHANGE` 事件驱动
- **F1.2 设备信息**：盘符、卷标、文件系统（`GetVolumeInformation`）、容量（`GetDiskFreeSpaceEx`）、序列号
- **F1.3 机型匹配**：机型为独立状态——**程序启动时取 ini 的 `DEFAULTMODEL`**，运行中操作员可随时在下拉菜单更改；设备插入时直接使用"当前选中机型"的配置节执行

### 2.2 操作引擎（统一接口：`execute(ctx, params) -> StepResult`）

| 操作 | 说明 |
|---|---|
| `write` | 写入，两种模式（`WRITEMODE`）：**COPY**——按 `SRCPATH`（支持通配符）简单复制到 `DSTPATH`；**PROCESSED**——按 `TEMPLATEPATH` 模板将 `${SN}`/`${MAC}`/`${DATE}` 等占位符按台替换生成不同文件后写入，输出文件名可用模式（如 `GTB907785${SN}.cfg`），写入前后哈希校验 |
| `read` | 按 `READFILE` 读取 U 盘文件，**经程序判定（READRULE）后才显示**：PASS → PANE_FILE 显示内容 + 绿色徽标；FAIL → 只显示失败原因 + 红色徽标 + 出错标记联动 |
| `format` | FAT32 / exFAT 格式化（fmifs.dll `FormatEx`，回退 format.com），卷标设置；`CONFIRM=1` 弹窗二次确认 |
| `usbinfo` | **USB 通讯模式识别**（非测速）：USB 2.0 / 3.x 模式、接口类型、Type-C 正反插识别；多途径探测，取不到标注"未知"不报错 |
| `read_label` | 读取 U 盘卷标/文件系统/序列号，刷新 PANE_LABEL 显示行并写日志 |
| `write_label` | 将机型配置的 `VOLUME` 写入 U 盘（`SetVolumeLabel`），写后回读比对确认 |
| `rwtest` | 写入测试文件 → 回读 → MD5 比对（读写质量检测） |
| `verify` | 对写入/复制后的文件做哈希校验 |
| `gen_serial` / `gen_mac` | 按台生成序列号 / MAC 地址等标识（Phase 3，规则待定），输出走 CMD-B |

**通配符模式匹配（读取与写入均支持）**：
- `READFILE=GTB907785*.txt` —— 固定格式 + 可变部分（`XXXX` 为每次换盘不同的序列号尾号）
- 语法：`*` 任意长度、`?` 单字符、`[0-9]` 字符集；`READPATTERN=REGEX` 可切换正则（如 `^GTB907785\d{4}\.txt$`）
- 多命中取舍 `MATCH_POLICY`：`NEWEST`（默认）/ `FIRST` / `STRICT`（报错列出全部）
- 命中的可变片段自动捕获为 `${MATCH_SN}` 等变量，供判定、日志、写入模板复用
- 未命中：CMD-A 输出 `[WARN] 未找到匹配…`，按失败策略处理并联动出错标记

### 2.3 配置文件（ini，全新格式）

```ini
[TOOLSINFO]
TITLE=U盘拷盘工具               ; 窗口标题
VERSION=1.0.0                  ; 软件版本（标题栏/关于/日志头）
ADMINPASS=                     ; 配置窗口管理员密码（或 ADMINPASS_HASH，二选一）
PRJNUM=3
PRJNAME1=Modeltype1            ; 内部键：与节名一一对应，程序结构依赖，永不改动
PRJNAME2=Modeltype2
PRJNAME3=Modeltype3
DISPLAYNAME1=拷盘-标准型       ; 显示名：界面/下拉/配置窗口显示，可随时改，缺失回退内部键
DISPLAYNAME2=拷盘-增强型
DISPLAYNAME3=检测-出厂
DEFAULTMODEL=GENERIC            ; 启动时默认选中的机型（内部键）
;（本节为格式示例；程序初始参考配置已按 4 种实际机型生成，见 profiles\UdiskTools.ini：
;  GENERIC=通用机型 / WT3 / N2010 / K2511S4，显示名用 DISPLAYNAME）

[Modeltype1]
; ---- 功能开关（每机型独立，0=禁用；界面按钮置灰，自动模式跳过并记日志）----
ENABLE_READ=1                  ; 读取（读取+判定）
ENABLE_WRITE=1                 ; 写入
ENABLE_FORMAT=1                ; 格式化
ENABLE_USBINFO=1               ; USB 通讯模式识别
; ---- 写入 ----
WRITEMODE=COPY                 ; COPY=简单复制 | PROCESSED=按台处理生成不同文件
SRCPATH=E:\temp\temp\*.img     ; COPY：复制源（支持通配符）
DSTPATH=test\                  ; 写入目标（U 盘内）
TEMPLATEPATH=templates\        ; PROCESSED：模板目录（${SN}/${MAC}/${DATE} 占位符）
; ---- 读取 ----
READFILE=GTB907785*.txt        ; 读取文件：通配符或固定名（; 结尾）
MATCH_POLICY=NEWEST            ; NEWEST/FIRST/STRICT
READPATTERN=WILDCARD           ; WILDCARD | REGEX
READRULE=MUST_CONTAIN:DeviceID;MIN_LEN:10   ; 判定规则链：通过才显示
; ---- 其他 ----
CONFIRM=1                      ; 格式化前确认
FILESYSTEM=FAT32|EXFAT
VOLUME=                        ; 卷标（格式化/写入标签用）
HASH=md5                       ; 校验算法
```

**执行策略**：`AUTORW=1` 插盘自动执行读写类操作、`AUTOCOPY=1` 插盘自动写入；失败策略 `stop / continue / retry:n`；多盘插入排队执行，可选按设备并行。

### 2.4 界面（Win32 原生控件）

**窗口标题栏**：`TITLE vVERSION`（如"U盘拷盘工具 v1.0.0"）。**不做托盘常驻**，关闭窗口即退出（任务运行中先确认）。

**主窗口 `WND_MAIN` 分区与控件**（全部登记 `ui/resource.h`，`PANE_`/`IDC_` 前缀）：

| 区域 | 控件 ID | 说明 |
|---|---|---|
| PANE_TOP | `IDC_COMBO_MODEL` | 机型下拉框（显示 DISPLAYNAME，启动取 DEFAULTMODEL，随时可改） |
| PANE_TOP | `IDC_BTN_START` / `IDC_BTN_STOP` / `IDC_BTN_FORMAT` | 开始 / 停止 / 格式化（受 ENABLE_* 控制，禁用置灰） |
| PANE_TOP | `IDC_BTN_READLABEL` / `IDC_BTN_WRITELABEL` | 读取标签 / 写入标签 |

**按钮显示名按机型配置**：每个按钮的**默认名固定**（开始/停止/格式化/读取标签/写入标签），但每种机型节内可用 `BTN_*` 键覆盖显示文字，例如通用机型 `[GENERIC]` 中 `BTN_WRITELABEL=升级` 后，该机型下"写入标签"按钮显示为"升级"。内部控件 ID 与日志记录的操作名永不改变，仅界面文字变化；未定义 `BTN_*` 的按钮用默认名。
| PANE_TOP | `IDC_BTN_CONFIG` | 配置按钮 → 弹出 WND_CONFIG |
| PANE_TOP | `IDC_CHK_TOPMOST` | **窗口置顶复选框**：选中后 `SetWindowPos(HWND_TOPMOST)` 使窗口始终在桌面最前端、不被其它程序窗口遮挡；取消恢复 `HWND_NOTOPMOST`；状态写入日志 |
| PANE_TOP | `IDC_STC_MODE` | 手动/自动模式标记：绿●自动（插盘触发）/ 蓝●手动（按钮/热键），空闲显示上次模式置灰 |
| PANE_LABEL | `IDC_STC_LABEL` | 标签信息行（卷标/文件系统/序列号） |
| PANE_STATUS | `IDC_STC_DEVICE` | 设备状态（盘符/容量/FS/插入状态/当前机型） |
| PANE_STATUS | `IDC_STC_ERROR` | 出错徽标（红色"✕ 出错"+ 摘要，无错隐藏） |
| PANE_TASK | `IDC_PBAR_TASK` | 进度条 |
| PANE_CMDS | `IDC_EDIT_ENGINE`（CMD-A） | 操作引擎输出（只读，Consolas，INFO/OK/WARN/ERR 分级配色，预留交互命令） |
| PANE_CMDS | `IDC_EDIT_IDENT`（CMD-B） | 标识生成输出（gen_serial/gen_mac 按台结果，独立于 CMD-A） |
| PANE_FILE | `IDC_STC_READVERDICT` | 读取判定徽标：绿 PASS / 红 FAIL |
| PANE_FILE | `IDC_EDIT_FILE` | READFILE 内容显示区 |
| 全局 | `ID_HOTKEY_T` / `ID_HOTKEY_C` | 热键：T=读写测试，C=写入 |

**出错标记三处联动**：① `IDC_STC_ERROR` 徽标点亮；② CMD 区对应行红色高亮（`[ERR]`）；③ 窗口标题追加 `[!]`。下次任务成功或操作员确认后熄灭，错误保留在日志。

**配置窗口 `WND_CONFIG`（`IDC_BTN_CONFIG` 弹出）**：
- 打开即显示全部配置，**默认只读**（`ES_READONLY` + 灰底，标注"只读（未授权）"，状态控件 `IDC_STC_CFGSTATE`）
- [解锁编辑]（`IDC_BTN_UNLOCK`）→ 密码框（`IDC_CFG_EDIT_PASS`，掩码）比对 `ADMINPASS`/`ADMINPASS_HASH`：正确 → 全部键可编辑，[保存]（`IDC_BTN_SAVECFG`）写回 ini（保留注释与未涉及键）；错误 → 保持只读；**连错 3 次锁定 5 分钟**并记日志
- 机型列表显示 DISPLAYNAME，内部键 PRJNAME 对操作员隐藏不可编辑
- 配置窗口控件 `IDC_CFG_` 前缀；解锁/锁定/保存均写日志（不含密码）

### 2.5 日志（强制，不可关闭）
- `logs\YYYYMMDD.log` 按日期滚动：时间、机型（内部键+显示名）、盘符、操作、结果、耗时、软件版本
- rwtest 校验结论、usbinfo 通讯模式信息可导出 CSV
- CMD-A/B 与文件日志同步输出

### 2.6 DPI 适配（从第一行代码做对）
1. Manifest 声明 `PerMonitorV2`；启动时 `SetProcessDpiAwarenessContext`（失败回退 V1 / System Aware）
2. 所有窗口/控件/字体尺寸经 `GetDpiForWindow()` 换算（`Scale(dpi, px)` 宏），禁止写死像素
3. 处理 `WM_DPICHANGED`：重建字体、`SetWindowPos` 重排
4. 图标/位图多尺寸资源（16/20/24/32/48），避免拉伸模糊
5. 验收：100% / 125% / 150% / 200% 四档缩放无虚化（M2 里程碑项）

---

## 3. 技术方案

| 项 | 选择 |
|---|---|
| 语言 | C11，纯 Win32 API（无第三方 GUI 框架） |
| 编译 | MinGW-w64 (gcc)，CMakeLists.txt / Makefile |
| 字符集 | 全程 Unicode（W 系列 API，内部 UTF-16），ini 兼容 GBK/UTF-8 读取 |
| 哈希 | 自带 md5.c（公共域实现） |
| 格式化 | fmifs.dll `FormatEx`，回退 format.com |
| 热键 | RegisterHotKey |
| 并发 | CreateThread + 互斥锁：工作线程执行 IO，UI 线程不卡 |
| 交付 | 单 exe（数百 KB 量级），免安装免运行库 |

**代码结构**：
```
udisktools/
├── src/
│   ├── main.c          # WinMain、消息循环、热键
│   ├── ui.c/h          # WND_MAIN / WND_CONFIG、控件、DPI 缩放
│   ├── device.c/h      # 驱动器枚举、卷信息、插拔轮询、usbinfo
│   ├── ops.c/h         # write/read/format/usbinfo/read_label/write_label/rwtest/verify/gen_*
│   ├── ini.c/h         # ini 解析与写回（保留注释）
│   ├── pattern.c/h     # 通配符/正则匹配、可变片段捕获、占位符替换
│   ├── rule.c/h        # READRULE 判定规则链
│   ├── log.c/h         # 日志与 CSV
│   ├── md5.c/h
│   └── util.c/h        # 编码转换、路径、字符串
├── profiles/           # 示例 ini
├── ui/resource.h       # 控件 ID 登记
└── CMakeLists.txt
```

---

## 4. 开发计划（6 周）

### Phase 1 — 引擎与 CLI（第 1–2 周）
- ini 解析写回、机型双层映射、通配符/正则匹配、READRULE 规则链
- 设备枚举与卷信息、write（COPY/PROCESSED）、read+判定、rwtest
- 控制台模式跑通全流程，输出结果

**M1**：命令行指定机型+盘符，完成写入/读取判定/读写测试并输出结果。

### Phase 2 — GUI 集成（第 3–4 周）
- WND_MAIN 全部分区与控件、CMD-A/B 双输出、模式/出错标记
- 插盘自动触发、热键 T/C、CONFIRM 确认
- format、read_label/write_label、WND_CONFIG（只读+密码解锁）
- DPI 四档验收

**M2**：GUI 完整可用，100%–200% 无虚化。

### Phase 3 — 增强与发布（第 5–6 周）
- usbinfo、verify、gen_serial/gen_mac（CMD-B）、CSV 导出
- 异常处理：拔盘中断、磁盘满、只读盘、FAT32 4GB 限制、劣质盘假死超时
- 打包发布：单 exe + 示例 ini + 使用说明

**M3**：v1.0 发布。

---

## 5. 风险与对策

| 风险 | 对策 |
|---|---|
| C 语言 GUI/格式化 API 复杂度高 | Win32 原生控件 + fmifs.dll，不引第三方框架；先 CLI 后 GUI |
| exFAT 格式化部分系统需提权 | 不做全局管理员要求；失败时明确提示并引导按需提权 |
| 轮询插拔有延迟/漏检 | 1s 轮询成熟可靠；Phase 3 可升级 WM_DEVICECHANGE |
| USB 模式识别依赖硬件/驱动 | 多途径探测，取不到标注"未知"不报错 |
| 通配符/长路径/中文编码 | util.c 集中处理，全程宽字符 |
| 劣质 U 盘卡死/掉盘 | 所有 IO 设超时；监听设备移除强制作废任务 |

---

## 6. 验收标准（摘要）
1. 插入 U 盘（自动模式开启时）数秒内按当前选中机型自动执行，手动模式按钮/热键立即响应
2. READRULE 判定 PASS 才显示内容；FAIL 触发三处出错标记
3. 写入后 verify 哈希比对 100% 一致；PROCESSED 模式每台生成的文件内容不同且符合模板
4. 通配符读取：`GTB907785*.txt` 在换盘（序列号尾号变化）后仍能正确命中
5. 配置窗口默认只读，密码解锁后可编辑，连错 3 次锁定
6. ENABLE_* 禁用的操作：按钮置灰、自动跳过、日志记录
7. 每次任务可查日志（含版本号）；GUI 在 100%–200% 四档 DPI 下无虚化
8. 任务中拔盘：标记 ABORTED，软件不崩溃，可继续下一次任务

---

## 7. 待确认事项
- [ ] `gen_serial` / `gen_mac` 生成规则：流水号段、MAC 池来源、是否需要防重复持久化

## 8. 已确认决议
- ✅ 不强制管理员权限（仅格式化按需提权）
- ✅ 日志强制输出，不可关闭
- ✅ 不做托盘常驻
- ✅ 不做精准测速，改为 USB 通讯模式识别（usbinfo）
- ✅ 全新开发，无历史包袱；机型内部键与显示名双层映射（初始 4 机型：GENERIC=通用机型 / WT3 / N2010 / K2511S4，见 profiles\UdiskTools.ini）
- ✅ 编译工具链 MinGW-w64
- ✅ 机型匹配与轮询解耦（DEFAULTMODEL / 下拉随时改）
- ✅ 双 CMD 显示区 + 全控件命名 + 手动/自动与出错状态标记
- ✅ 每机型 ENABLE_* 功能开关；读取先判定再显示；写入 COPY/PROCESSED 双模式
- ✅ 读取/写入支持通配符（固定格式+可变部分）
- ✅ 配置窗口密码保护（默认只读）
- ✅ 窗口置顶复选框（IDC_CHK_TOPMOST，HWND_TOPMOST/NOTOPMOST 切换）
- ✅ DPI Per-Monitor V2 从第一行代码做对
