# UdiskTools

Windows 可移动磁盘（U 盘）自动化操作工具。C11 + Win32 API 全新开发，单 exe 免运行库。

一个 exe + 一个 ini：按机型配置写入、读取判定、格式化、USB 通讯模式识别、标签读取等操作（按台标识生成、独立写标签为预留），界面实时显示当前机型的 AUTO_COPY/AUTO_TEST 状态与 READRULE 判定规则（只读），支持自动（插盘触发）/ 手动（按钮、热键）两种方式，全程日志留痕。

详细规划见 [docs/UdiskTools软件规划书.md](docs/UdiskTools软件规划书.md)，界面控件对照见 [docs/控件位置对照图.html](docs/控件位置对照图.html)（浏览器直接打开）；全部文档索引见 [docs/README.md](docs/README.md)。

## 当前状态（软件 v1.2.0）

> **版本说明（两套版本相互独立）**：**软件版本 1.2.0**（`src/version.h` `APP_VERSION`）显示于 `--version`、启动日志与窗口标题“（软件 v1.2.0）”；**配置文件版本**由 ini `[TOOLSINFO] VERSION` 决定（示例配置当前 1.0.0），显示于窗口标题“（配置 v…）”与 `--check-ini` 输出。升级程序**不会**自动修改配置文件版本。
> 窗口标题格式：`<TITLE>（软件 v1.2.0）（配置 v1.0.0）`；配置版本为空时显示 `<TITLE>（软件 v1.2.0）`。

✅ 已实现并通过编译（零警告）与启动冒烟测试：

| 模块 | 说明 |
|---|---|
| ini 配置解析 | 统一编码探测：UTF-8±BOM / UTF-16LE/BE±BOM / 无 BOM UTF-16 启发式 / GBK；损坏 UTF-8 与二进制拒绝并记原因；容量超限/截断/畸形/重复节写 WARN；双层机型映射（PRJNAME 内部键 / DISPLAYNAME 显示名）；保存时保留原编码/BOM/行尾/行内注释与其余内容 |
| 设备轮询 | 1 秒轮询 DRIVE_REMOVABLE；插/拔事件按“变化盘符位图 + 插入/拔出”上报（同周期先拔后插都不丢）；任务执行中插入的盘记为待处理，任务成功/按配置跳过结束后补执行 |
| 操作引擎 | write（COPY 复制 + MD5 校验；MD5 不一致自动重试一次，仍不一致删除坏副本并判失败；PROCESSED/非 md5/未配 SRCPATH/MD5 无法计算均判失败）/ read（通配符匹配 + READRULE 判定 + ${MATCH_1} 捕获；REGEX/未知规则/读中换盘判失败）/ format（fmifs FormatEx + 强制确认 + 卷标校验（FAT32/exFAT ≤11 字符）+ 格式化后刷新界面；确认后复核卷序列号/VID/SN，换盘即取消）/ usbinfo（USB Hub IOCTL 按 VID/PID 匹配端口识别 USB 2.0/3.x 速度；同 VID/PID 多设备或取不到时标注未知 + 原因）/ 标签读取（写卷标仅格式化后按 VOLUME 回写，无独立写标签操作）/ rwtest（8MB 写读 MD5 比对 + 速率；NO_BUFFERING/WRITE_THROUGH 直读介质，回退缓冲时日志标注）。**按配置正常屏蔽的步骤（ENABLE_*=0、READFILE 留空、AUTO_COPY/AUTO_TEST=0）仍判成功并计数；程序自行降级/未实现/无法校验一律判失败** |
| GUI | 全控件按 resource.h 命名，CMD-A 操作日志输出区 + CMD-B 输出区（预留：标识生成扩展），手动/自动模式标记，出错徽标三处联动，窗口置顶，快捷键 Ctrl+Alt+T=读写测试 / Ctrl+Alt+C=复制（勾选“快捷键使能”后注册）；窗口标题显示 `<TITLE>（软件 v1.2.0）（配置 v…）`；新增 **AUTO_COPY/AUTO_TEST 灰只读状态框**（`IDC_CHK_AUTO_COPY 1009` / `IDC_CHK_AUTO_TEST 1018`）与 **READRULE 判定规则标签**（`IDC_STC_READRULE 1109`，`AUTO_TEST=1` 时显示）；任务执行中 [停止] 可用（复制即时取消），关窗先停止并等待工作线程结束；任务结束日志含“执行 N 步 / 按配置跳过 M 步”，全部按配置跳过时显示“按配置跳过（未执行）” |
| 配置窗口 | 默认只读，密码解锁编辑（密码错误可无限重试、仅记录日志；不做次数锁定；未解锁时保存/恢复默认不可用），单键保存回写 ini（保留原编码/BOM/行尾、其余内容与行内注释；写后回读校验；保存/恢复默认都会检查重载结果，失败不再假报成功）；恢复默认先备份 .bak、原子写+回读校验；保存/恢复/重载后刷新标题与机型列表 |
| DPI | Per-Monitor V2 感知，字体/布局随 DPI 缩放，WM_DPICHANGED 动态重排 |
| 日志 | 强制输出 `logs\YYYYMMDD.log`（UTF-8），与界面输出同步；句柄常驻、共享读写删除、单文件 >8MB 轮转 `.log.1`、被占用时写 `_alt.log` 不丢行、`log_shutdown` 安全收尾 |
| 运行/单实例 | 单实例互斥：第二个实例激活现有窗口后退出（不重复开 GUI）；CLI：`--ini <path>` / `--check-ini`（不建 GUI，0/1，输出机型数与**配置文件版本**）/ `--version`（显示**软件版本 1.2.0** 与构建时间）/ `--help` |
| 资源/诊断 | `.rc` 版本信息 + 图标 + manifest（comctl32 v6 / PerMonitorV2 / longPathAware）；未处理异常写 `logs\crash_*.dmp`（dbghelp）；退出码 0 正常 / 1 消息循环失败 / 3 强制退出 |

可选 ini 键：`AUTORUN=1`（机型节内）启用插盘自动执行；`BTN_*=显示名` 按机型自定义按钮文字。

## 快速开始（开发）

```
# 依赖：MinGW-w64（gcc/llvm-mingw，需 windres 编译资源；dbghelp 用于崩溃转储）；构建可用 CMake 或 Make
git clone <repo-url>
cd udisktools

# 方式一：CMake
cmake -B build -G "MinGW Makefiles"
cmake --build build

# 方式二：直接 Make
make

# 运行（需管理员外的普通权限；ini 与 exe 同目录）
bin\udisktools_v1.2.0.exe
```

## 命令行

| 参数 | 说明 |
|---|---|
| `--ini <path>` | 指定配置文件（默认 exe 同目录 `UdiskTools.ini`；支持引号/相对路径） |
| `--check-ini` | 仅加载校验配置并退出：0=成功、1=失败（不建 GUI，可用于 CI/现场排查）；成功时输出机型数与**配置文件版本**（ini `[TOOLSINFO] VERSION`） |
| `--version`, `-v` | 显示**软件版本**（1.2.0）、FileVersion（1.2.0.0）与构建时间后退出（不加载 ini） |
| `--help`, `-h`, `-?` | 显示用法后退出 |

> 无参数启动 GUI；第二个实例会激活已运行的窗口并退出（单实例）。

## 文档

| 文档 | 路径 | 说明 |
|---|---|---|
| 文档索引 | [docs/README.md](docs/README.md) | 全部文档入口（按读者/用途分类） |
| 使用手册 | [docs/用户使用手册.md](docs/用户使用手册.md) | 面向操作员：安装启动、典型操作、FAQ |
| 配置手册 | [docs/配置文件使用手册.md](docs/配置文件使用手册.md) | ini 配置键、按钮/自动流程、配置窗口、排错 |
| UI 功能状态说明 | [docs/UI功能状态说明.md](docs/UI功能状态说明.md) | 逐控件 ID 状态、快捷键、状态机、预留清单 |
| 软件规划书 | [docs/UdiskTools软件规划书.md](docs/UdiskTools软件规划书.md) | 需求、功能规格、验收、Phase 计划 |
| 插拔全流程图 | [docs/插拔全流程图.html](docs/插拔全流程图.html) | 插盘→AUTO/MANUAL→整任务→结果→拔出复位 |
| 开发流程 | [docs/开发流程.md](docs/开发流程.md) | 构建、测试、CI 门禁、提交/分支规则 |
| 变更记录 | [docs/CHANGELOG.md](docs/CHANGELOG.md) | 审计修复批次 + 功能优化（软件 v1.2.0） |
| 代码审计报告 | [docs/UdiskTools-代码审计报告-20261024.md](docs/UdiskTools-代码审计报告-20261024.md) | 2026-10-24 审计报告（唯一有效版本） |

## 目录结构

```
udisktools/
├── src/            # 源码（main / gui / gui_config / ops / device / config / pattern / util / md5 / usbspeed）
├── ui/             # resource.h（控件与图标 ID 登记）
├── res/            # .rc / manifest / ico 资源（版本信息、图标、DPI/控件清单）
├── profiles/       # UdiskTools.ini（4 机型示例）+ UdiskTools_default.ini（内置默认，ini 缺失时启动/恢复默认共用）
├── docs/           # 文档（扁平结构）：索引/CHANGELOG/手册/UI说明/规划书/开发流程/流程图/对照图/审计报告 + archive/
├── tests/          # 测试（run_tests.ps1 驱动，见开发流程）
├── scripts/        # default_ini 生成/校验、辅助脚本
└── .github/        # CI（GitHub Actions）
```

> `temp_run/`（工作区根目录、**仓库外**）是**临时调试/测试运行目录**（含临时 exe/ini、日志与升级源文件副本）：**不纳入构建与清理流程，禁止删除或改动**；本仓库的目录结构只包含上表的 `udisktools/`。

## 分支与协作

见 [docs/开发流程.md](docs/开发流程.md)（构建/测试/CI 门禁/提交与分支规则）。
