# 策划方案 v2.0：UdiskTools — 两款旧软件合并重构（C 语言版）

> 版本：v2.0 ｜ 日期：2026-10-20 ｜ 取代：v1.0 通用方案（UdiskOps）
> 旧软件位置：`D:\workbuddy\WR_Udisk\temp\`

---

## 1. 旧软件功能分析

### 1.1 DiskRW_V2.0（IET_disktools，exe + ini）
**用途**：产线 U 盘拷盘/格式化工具，多项目配置。

已实现的功能：
| 功能 | ini 键 | 说明 |
|---|---|---|
| 多项目配置 | `PRJNUM` / `PRJNAME1..n` | 每个项目一个 ini 节，如 Test/Copy、PAWV/VE、PAW5000 等 6 个项目 |
| 文件复制 | `SRCPATH` → `DSTPATH` | 支持通配符（如 `*.img`）、目录结构 |
| 插盘自动读写 | `AUTORW=1` | 检测到 U 盘插入自动执行读写测试 |
| 插盘自动复制 | `AUTOCOPY` | V2.0 新增，自动拷贝参数 |
| 格式化 | `CONFIRM` / `FILESYSTEM` / `VOLUME` | FAT32 / exFAT，卷标（≤11 字节），CONFIRM=1 需确认 |
| 热键 | 键盘 T 键 | 触发读写测试 |
| 热键 | 键盘 C 键 | 触发复制（不区分大小写） |
| 运行要求 | — | 需以管理员身份运行；ini 与 exe 同目录 |

### 1.2 read-U（Python + tkinter）
**用途**：U 盘文件监控读取器。

已实现的功能：
- 从 `read-U.ini` 读取目标文件名（`source_file_name_01`，默认 `1_4_0_1.ID`）
- 轮询所有驱动器（1 秒间隔），识别可移动磁盘（`GetDriveType == DRIVE_REMOVABLE`）
- 找到目标文件 → 读取并显示内容（utf-8/gbk 多编码尝试）
- 拔盘/文件移除 → 清空显示；GUI 有开始/停止/退出按钮

### 1.3 两软件重叠与互补点
- **重叠**：都是 U 盘插拔检测 + 按 ini 配置动作
- **互补**：DiskRW 偏"写入/格式化/产线拷盘"，read-U 偏"读取显示/核对"
- **合并思路**：统一为一个配置驱动的引擎，"复制/格式化/读写测试"与"读文件显示"都变成配置里的**步骤**，一个 GUI 全部承载。

---

## 2. 合并后的软件设计（C 语言重写）

### 2.1 定位
**UdiskTools**：Windows 单文件 GUI 工具（Win32 API，无第三方运行时依赖），一个 exe + 一个 ini，覆盖两款旧软件的全部功能模式，并补充 USB 通讯模式识别与日志能力。**说明：旧软件仅作为功能模式的设计参考，本软件为完全重构，不存在迁移与向后兼容负担。**

### 2.2 功能规格

#### A. 设备层（共用）
- A1 轮询驱动器（沿用 read-U 的成熟轮询方案，1s 间隔，`GetLogicalDrives` + `GetDriveType`），Phase 2 可升级为 `WM_DEVICECHANGE` 事件驱动
- A2 设备信息：盘符、卷标、文件系统（`GetVolumeInformation`）、容量（`GetDiskFreeSpaceEx`）、序列号
- A3 插入检测 → 按当前项目配置自动触发（AUTORW / AUTOCOPY 语义保留）

#### B. 操作引擎（每个操作一个 C 函数，统一接口）
| 操作 | 来源 | 说明 |
|---|---|---|
| `copy` | DiskRW | SRCPATH→DSTPATH，支持通配符、递归目录 |
| `format` | DiskRW | FAT32/exFAT 格式化（调 `FormatEx` / shell `format` 封装），卷标设置，CONFIRM 二次确认 |
| `rwtest` | DiskRW (T 键) | 写入测试文件→回读→比对（MD5），即原"读写测试" |
| `usbinfo` | 新增（替代 speedtest） | **USB 通讯模式识别**（非精准测速）：识别 USB 2.0 / USB 3.x 通讯模式、接口类型、Type-C 正反插识别等信息，结果输出到 CMD-A 并写入日志 |
| `readfile` | read-U | 按 `source_file_name` 读取文件，**先经程序判定（READRULE）再显示**：判定通过才在 PANE_FILE 显示并标记 PASS，判定失败标记 FAIL 并点亮出错标记，原始内容不直接展示 |
| `write` | DiskRW + 扩展 | **两种写入模式**（`WRITEMODE`）：① `COPY`——简单复制 SRCPATH→DSTPATH（原 copy 功能）；② `PROCESSED`——由程序处理后生成不同的文件再写入（如按台替换 SN/MAC/日期等占位符后生成每台唯一的文件，配合 gen_serial/gen_mac 使用） |
| `verify` | 新增 | 对复制/写入后的文件做哈希校验 |
| `gen_serial` / `gen_mac` | 新增（Phase 3） | 为每台机器生成序列号 / MAC 地址等标识信息并写入 U 盘（在 CMD-B 窗口运行显示，生成规则由配置或外部命令驱动，预留扩展） |
| `read_label` | 新增 | **读取标签**：读取 U 盘卷标、文件系统、序列号等标签信息，结果显示在 PANE_LABEL 区并写入日志 |
| `write_label` | 新增 | **写入标签**：将配置的卷标（`VOLUME` 键）写入 U 盘（`SetVolumeLabel`），写入前后自动回读确认；CONFIRM=1 时弹窗二次确认 |

**权限说明（重要）**：日常操作（复制、读写测试、读文件、测速）**不需要管理员权限**，普通用户身份即可运行；仅"格式化"操作在部分系统上可能需要提权——软件运行时按需处理（失败时给出明确提示并引导），不做全局管理员要求。

#### C. 配置文件（合并两套 ini，保持向后兼容）
新 ini 仍分节，每节 = 一个项目；在原 DiskRW 键基础上合并 read-U 的键：

```ini
[TOOLSINFO]
TITLE=U盘拷盘工具               ; ← 窗口标题（GUI 标题栏显示）
VERSION=1.0.0                  ; ← 软件版本（标题栏 + 关于 + 日志头）
ADMINPASS=                     ; ← 配置窗口管理员密码（留空=无密码保护；建议存哈希）
PRJNUM=3
PRJNAME1=Modeltype1            ; ← 内部键：与节名一一对应，程序结构依赖它，改了会破坏配置
PRJNAME2=Modeltype2
PRJNAME3=Modeltype3
DISPLAYNAME1=拷盘-标准型       ; ← 显示名：操作员在界面看到的名字，可随时改，不影响程序
DISPLAYNAME2=拷盘-增强型
DISPLAYNAME3=检测-出厂
DEFAULTMODEL=Modeltype1        ; ← 程序启动时默认选中的机型（内部键）

[Modeltype1]
; ---- 功能开关（每机型独立，0=禁用该操作，界面按钮同步置灰）----
ENABLE_READ=1                  ; 读取（读取+判定）
ENABLE_WRITE=1                 ; 写入
ENABLE_FORMAT=1                ; 格式化
ENABLE_USBINFO=1               ; USB 通讯模式识别（USB2.0/3.x、Type-C 正反插等）
; ---- 写入配置 ----
WRITEMODE=COPY                 ; COPY=简单复制 | PROCESSED=程序处理后生成不同文件写入
SRCPATH=E:\temp\temp\*.img     ; COPY 模式：复制源（支持通配符）
DSTPATH=test\                  ; 写入目标（U 盘内）
TEMPLATEPATH=templates\        ; PROCESSED 模式：模板目录，占位符 ${SN}/${MAC}/${DATE} 等按台替换
; ---- 读取配置 ----
READFILE=GTB907785*.txt        ; 读取的文件：支持通配符（见下），兼容旧式固定文件名（; 结尾，兼容 read-U 格式）
MATCH_POLICY=NEWEST            ; 通配符多命中时取舍：NEWEST(默认)/FIRST/STRICT
READPATTERN=WILDCARD           ; WILDCARD(* ? [0-9]) | REGEX（如 ^GTB907785\d{4}\.txt$）
READRULE=MUST_CONTAIN:DeviceID;MIN_LEN:10   ; ← 判定规则（分号分隔多条）：程序判定通过才显示
; ---- 其他 ----
CONFIRM=1                      ; 格式化前确认
FILESYSTEM=FAT32|EXFAT
VOLUME=                        ; 卷标（供格式化/写入标签使用）
HASH=md5                       ; 校验算法
```

> 设计要点：
> - **每机型功能开关**：`ENABLE_*` 四项独立控制读取/写入/格式化/USB 识别；禁用时界面对应按钮置灰、自动模式跳过该步骤，日志记录"已禁用"
> - **读取判定**：`READRULE` 规则链（包含关键字、最小长度、正则预留等），判定 PASS/FAIL 显示在 PANE_FILE 头部，FAIL 触发出错标记三处联动
> - **通配符匹配（读取与写入均支持）**：
>   - 读取侧 `READFILE` 支持通配符模式，如 `GTB907785*.txt`——文件名满足固定格式又含可变部分（`XXXX` 为每次换盘后不同的机器序列号尾号）。程序扫描目标目录（U 盘根目录或指定子目录），按 `MATCH_POLICY` 取命中文件：`NEWEST`（默认，取最新）/ `FIRST` / `STRICT`（多个命中则报错并列出全部）
>   - 通配符语法：`*` 任意长度、`?` 单字符、`[0-9]` 字符集；可加 `READPATTERN=REGEX` 切换为正则（如 `^GTB907785\d{4}\.txt$`）
>   - **可变部分捕获**：命中的可变片段自动存为变量（如 `${MATCH_SN}`），供 READRULE 判定、CMD-A 日志（`命中文件 GTB9077851234.txt, SN=1234`）与 PROCESSED 写入模板复用
>   - 写入侧 `SRCPATH` 本就支持通配符（`*.img`）；`PROCESSED` 输出文件名同样可用模式（如 `GTB907785${SN}.cfg`），实现"固定格式+可变部分"的对称命名
>   - 未命中：CMD-A 输出 `[WARN] 未找到匹配 GTB907785*.txt`，按失败策略（stop/continue/retry）处理并联动出错标记
> - **写入两种模式**：`COPY` 兼容旧 DiskRW 行为；`PROCESSED` 按台生成唯一文件（占位符替换），写入前后均做哈希
> - **标题与版本**：`TITLE`/`VERSION` 集中在 `[TOOLSINFO]`，窗口标题栏显示"TITLE vVERSION"，日志每条任务记录版本号
> - **机型名双层映射**：`PRJNAMEN`（内部键，与 ini 节名一一对应，**程序结构/日志内部引用/配置关联全部基于它，永不改动**）→ `DISPLAYNAMEN`（显示名，界面下拉框、按钮提示、操作员可见之处显示它）。显示名缺失时回退显示内部键。改显示名只动 DISPLAYNAME 行，对整体程序零影响
> - **配置密码**：`ADMINPASS` 支持明文（现场简单场景）或哈希（`ADMINPASS_HASH`），二者只取其一

> 命名规范：机型占位符统一为 `ModeltypeN`（N=1,2,3…），ini 节名、界面下拉框、日志中的机型字段全部使用同一命名，替换真实机型时只改 ini 即可，代码零改动。

> **机型匹配时机（重要，与轮询解耦）**：当前机型在**程序启动时由配置文件指定**（ini 中 `DEFAULTMODEL` 键），或由**操作员在运行中通过下拉菜单随时更改**。机型选择是独立的操作员决策状态，**不参与 U 盘检测轮询逻辑**——轮询只负责发现设备；设备插入时直接使用"当前选中的机型"执行其配置节，不在轮询中做机型推断。

**重构声明**：旧 ini 格式与两套旧软件均**不要求兼容、不做导入**，仅作为功能模式参考；新 ini 按上文全新设计，键名与节结构由本方案统一定义。

#### D. 界面（Win32 原生，合并两软件 UI，v2：分区命名 + 状态标记）

**分区与控件命名规范**：窗口标题栏显示 **"TITLE vVERSION"**（取自 ini 的 `[TOOLSINFO]`，如"U盘拷盘工具 v1.0.0"）。窗口 `WND_MAIN`，区域 `PANE_` 前缀，控件 `IDC_` 前缀，全部登记到 `ui/resource.h`，后期交互/自动化测试/二次开发统一按 ID 引用。

| 区域 | 控件 ID | 说明 |
|---|---|---|
| PANE_TOP | `IDC_COMBO_MODEL` | 机型下拉框（启动取 DEFAULTMODEL，操作员可随时改） |
| PANE_TOP | `IDC_BTN_START` / `IDC_BTN_STOP` / `IDC_BTN_FORMAT` | 开始 / 停止 / 格式化（**受 ENABLE_* 开关控制：禁用的按钮置灰**） |
| PANE_TOP | `IDC_BTN_READLABEL` / `IDC_BTN_WRITELABEL` | **读取标签 / 写入标签**：读取或写入 U 盘卷标（写入目标取当前机型配置节的 `VOLUME` 键；写入前自动回读校验，结果输出到 CMD-A） |
| PANE_LABEL | `IDC_STC_LABEL` | 标签信息显示行（卷标 / 文件系统 / 序号，由 read_label 刷新） |
| PANE_FILE | `IDC_STC_READVERDICT` | **读取判定结果徽标**：绿 PASS / 红 FAIL（READRULE 判定结论），FAIL 联动出错标记 |
| PANE_TOP | `IDC_BTN_CONFIG` | **配置按钮**：弹出配置窗口（见下） |
| PANE_TOP | `IDC_STC_MODE` | **手动/自动模式标记**：绿●"自动模式"（插盘触发）/ 蓝●"手动模式"（按钮/热键触发），随触发方式实时切换 |
| PANE_STATUS | `IDC_STC_DEVICE` | 设备状态行（盘符/卷标/容量/文件系统/插入状态/当前机型） |
| PANE_STATUS | `IDC_STC_ERROR` | **出错标记**：红色"✕ 出错"徽标 + 具体错误摘要；无错时隐藏 |
| PANE_TASK | `IDC_PBAR_TASK` | 进度条 |
| PANE_CMDS | `IDC_EDIT_ENGINE`（CMD-A） | 操作引擎输出（只读，等宽，分级配色） |
| PANE_CMDS | `IDC_EDIT_IDENT`（CMD-B) | 标识生成输出（gen_serial/gen_mac 按台结果，独立于 CMD-A） |
| PANE_FILE | `IDC_EDIT_FILE` | READFILE 文件内容显示区 |
| 全局 | `ID_HOTKEY_T` / `ID_HOTKEY_C` | 热键：T=读写测试，C=复制 |

**状态标记规则**：
- **模式标记**（IDC_STC_MODE）：本次执行由 `AUTORW/AUTOCOPY` 触发 → 绿色"自动模式"；由按钮/热键触发 → 蓝色"手动模式"；空闲时显示上次模式并置灰
- **出错标记三处联动**：① IDC_STC_ERROR 徽标点亮（红色 + 错误摘要，悬停/点击可定位 CMD 区错误行）；② CMD 区对应行红色高亮（`[ERR]` 前缀）；③ 窗口标题追加 `[!]`，最小化时也可见
- 恢复正常（下一次任务成功或操作员点击确认）后标记熄灭，错误记录保留在日志中

**配置窗口（`WND_CONFIG`，由 `IDC_BTN_CONFIG` 弹出）**：
- 打开即显示全部配置信息（`[TOOLSINFO]` + 各机型节），**默认只读模式**：所有编辑框 `ES_READONLY` + 灰底，窗口标注"只读（未授权）"
- 点击 [解锁编辑] → 弹密码输入框（`IDC_EDIT_PASS`，`ES_PASSWORD` 掩码）→ 与 `ADMINPASS`/`ADMINPASS_HASH` 比对：
  - **密码正确** → 进入编辑模式：显示名、功能开关、路径、规则等所有键可修改；[保存] 写回 ini（原样保留注释与未涉及键）；连续错误 3 次锁定配置窗口 5 分钟并记日志
  - **密码错误或取消** → 保持只读，仅可查看、不可修改、不可导出
- 机型名双层映射在配置窗口内体现：下拉/列表显示 `DISPLAYNAMEn`，编辑保存时程序只改对应 `DISPLAYNAMEn` 行，内部键 `PRJNAMEn` 对操作员隐藏且不可编辑
- 配置窗口所有控件同样 `IDC_CFG_` 前缀（如 `IDC_CFG_LIST_MODELS`、`IDC_CFG_EDIT_PASS`、`IDC_BTN_UNLOCK`、`IDC_BTN_SAVECFG`），受 DPI 规则约束
- 保存/解锁/锁定均写入日志（含操作时间，不含密码）

布局：顶部 PANE_TOP（按钮+机型+模式标记）→ PANE_STATUS（设备状态+出错徽标）→ PANE_LABEL（标签信息行）→ PANE_TASK（进度条）→ PANE_CMDS（CMD-A/B 左右并排）→ PANE_FILE。**不做托盘常驻**，关闭窗口即退出（任务运行中先确认）。

#### D2. DPI 适配（从第一行代码就考虑，禁止后补）
Windows 高 DPI 下不做适配会出现界面模糊（位图拉伸虚化）。措施：
1. **声明 Per-Monitor V2 DPI 感知**：嵌入应用程序清单（manifest）`<dpiAwareness>PerMonitorV2</dpiAwareness>`，而非仅 system aware
2. **启动即设置**：`SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`（Win10 1703+），失败则回退 Per-Monitor V1 / System Aware
3. **所有尺寸按 DPI 缩放**：窗口/控件尺寸、字体大小一律通过 `GetDpiForWindow()` 换算，禁止写死像素值（封装 `Scale(dpi, px)` 宏）
4. **字体**：使用 `SystemParametersInfo(SPI_GETNONCLIENTMETRICS)` 或按 DPI 直接创建 `CreateFontIndirect`，收到 `WM_DPICHANGED` 时重建字体并 `SendMessage(WM_SETFONT)`
5. **响应 DPI 变化**：处理 `WM_DPICHANGED`，按建议矩形 `SetWindowPos` 移动缩放窗口，并重排布局
6. **图标/位图资源**：提供多尺寸资源（16/20/24/32/48px）由系统挑选，避免拉伸模糊
7. **测试基准**：100%、125%、150%、200% 四档缩放下验收界面清晰度（纳入 M2 里程碑验收项）

#### E. 日志与报告（必备功能，必须保留）
- **运行日志为强制要求**：每次操作（含插拔事件、复制、读写测试、格式化、读取文件）都必须写入日志，不可关闭
- 日志写 `logs\YYYYMMDD.log`（时间、盘符、操作、结果、耗时），按日期滚动
- rwtest 结果含校验结论、usbinfo 结果含通讯模式信息，可导出 CSV
- 界面日志区与文件日志同步输出

### 2.3 技术选型
| 项 | 选择 |
|---|---|
| 语言 | **C11**，纯 Win32 API（GUI 用原生控件，无框架依赖） |
| 编译器 | **MinGW-w64（gcc）**（已确认）；CMakeLists.txt 或 Makefile 构建 |
| MD5 | 自带轻量 md5.c 实现或引用公共域实现 |
| 格式化 | 优先 `shmexf/fmifs.dll` 的 `FormatEx`（同 DiskRW 做法），回退调用 `format.com` |
| 热键 | `RegisterHotKey` |
| 线程 | `CreateThread` + 互斥锁（工作线程执行 IO，UI 线程不卡） |
| 目标 | Windows 10/11 x64，单 exe（几百 KB 量级），免安装免运行库 |

### 2.4 代码结构（规划）
```
udisktools/
├── src/
│   ├── main.c          # WinMain、消息循环、热键
│   ├── ui.c/h          # 界面创建与刷新
│   ├── device.c/h      # 驱动器枚举、卷信息、插拔检测
│   ├── ops.c/h         # copy / write / read / format / rwtest / usbinfo / read_label / write_label / gen_serial / gen_mac
│   ├── ini.c/h         # ini 解析（兼容两套旧格式）
│   ├── log.c/h         # 日志与 CSV 报告
│   ├── md5.c/h         # 哈希
│   └── util.c/h        # 编码转换、通配符匹配、路径拼接
├── profiles/           # 示例 ini（含迁移自 IET_disktools.ini 的 6 个项目）
└── CMakeLists.txt
```

---

## 3. 开发计划

### Phase 1 — 引擎与 CLI 化验证（第 1–2 周）
- ini 解析（兼容两种旧格式）、驱动器枚举与卷信息
- copy（通配符/递归）、readfile（多编码）、rwtest（写读比对）
- 先以控制台模式跑通全部操作

**M1**：命令行输入项目名 + 盘符，可执行写入/读写测试/读取判定，输出结果。

### Phase 2 — GUI 集成（第 3–4 周）
- Win32 界面：机型选择、设备状态、进度、CMD-A/B 双输出区
- 插盘自动触发（AUTORW/AUTOCOPY）、热键 T/C、CONFIRM 确认对话框
- format（FAT32/exFAT + 卷标）、配置窗口（密码解锁）

**M2**：功能覆盖两款旧软件全部功能模式（重构实现）；GUI 在 100%–200% DPI 四档缩放下无虚化。

### Phase 3 — 增强与发布（第 5–6 周）
- usbinfo（USB 2.0/3.x、Type-C 正反插识别）、verify、CSV 报告
- gen_serial / gen_mac 按台标识生成（CMD-B）
- 异常处理：拔盘中断、磁盘满、只读、FAT32 4GB 限制提示
- 打包：单 exe + 示例 ini + 使用说明

**M3**：v1.0 发布。

---

## 4. 旧软件功能参考表（仅设计参考，非迁移）

> 新软件为完全重构，下表仅说明功能模式的来源出处，不存在代码/配置迁移。

| 功能模式 | 参考来源 | 新软件落点 |
|---|---|---|
| 多机型分节 ini | DiskRW `[TOOLSINFO]` | ini 节结构（ModeltypeN + DISPLAYNAME 双层映射） |
| SRCPATH/DSTPATH 复制 | DiskRW | `write`（WRITEMODE=COPY） |
| AUTORW 插盘自动执行 | DiskRW | 设备事件 → 自动模式 |
| CONFIRM/FILESYSTEM/VOLUME | DiskRW | `format` 操作 |
| T/C 热键 | DiskRW | RegisterHotKey 保留 |
| 读指定文件并显示 | read-U | `read` 操作 + READRULE 程序判定 |
| 1s 轮询 + DRIVE_REMOVABLE | read-U | 设备层（Phase 3 可选升级 WM_DEVICECHANGE） |
| 内容显示区 | read-U tkinter | Win32 PANE_FILE 文本区 |

---

## 5. 风险与对策

| 风险 | 对策 |
|---|---|
| C 语言 GUI/格式化 API 复杂度高 | 用 Win32 原生控件 + fmifs.dll，不引第三方框架；先 CLI 后 GUI 降低风险 |
| exFAT 格式化在部分系统需提权 | 不做全局管理员要求：格式化失败时给出明确提示并引导按需提权 |
| 轮询检测插拔有延迟/漏检 | 保留 1s 轮询（旧软件已验证该模式可用）；Phase 3 加 WM_DEVICECHANGE |
| USB 通讯模式识别依赖硬件/系统 | usbinfo 多途径探测（注册 Bus/Device 速率 + 驱动查询），取不到时标注"未知"而非报错 |
| 通配符/长路径/中文编码坑 | util.c 集中处理，统一用 W 系列 Unicode API（宽字符），内部 UTF-16 |
| ini 含中文显示名 | 全程宽字符读写，兼容 GBK/UTF-8 ini |

---

## 6. 待确认事项
- [ ] gen_serial / gen_mac 的生成规则（流水号段、MAC 池来源等）

## 7. 已确认事项
- ✅ 运行**不强制要求管理员权限**（仅格式化等个别操作按需提权，失败时给出提示）
- ✅ **日志功能必须保留**，且为强制输出（所有操作必须写日志，不可关闭）
- ✅ **不做托盘常驻**，关闭窗口即退出
- ✅ **不做精准测速**，改为 USB 通讯模式识别（USB 2.0 / 3.x、Type-C 正反插等，`ENABLE_USBINFO`）
- ✅ **完全重构**：旧软件仅作功能模式参考，不做迁移与向后兼容
- ✅ 编译工具链：**MinGW-w64（gcc）**
- ✅ 机型名双层映射（PRJNAME 内部键 + DISPLAYNAME 显示名）
- ✅ 配置窗口密码保护（默认只读）
