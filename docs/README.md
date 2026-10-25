# UdiskTools 文档索引

> 版本：软件 v1.2.0（2026-10-24 工作树；配置文件版本独立，见 ini `[TOOLSINFO] VERSION`）　维护：文档与代码同批更新（见 [开发流程.md](开发流程.md)）
> 本文件是 `docs/` 的入口索引；所有文档与本文同目录（仅 `archive/` 为历史归档子目录），链接可直接在仓库/IDE/浏览器中打开。

## 1. 按用途找文档

| 读者/用途 | 文档 | 路径 | 内容摘要 |
|---|---|---|---|
| 产线/运维 | 用户使用手册 | [用户使用手册.md](用户使用手册.md) | 面向操作员：安装启动、界面速览、典型操作、配置窗口、FAQ |
| 产线/运维 | 配置文件使用手册 | [配置文件使用手册.md](配置文件使用手册.md) | ini 全键说明、按钮↔配置对照、自动流程、配置窗口、常见错误排查 |
| 产线/测试/研发 | UI 功能状态说明 | [UI功能状态说明.md](UI功能状态说明.md) | 逐控件 ID 状态、快捷键语义、配置窗口只读规则、任务状态机、预留/限制清单（**逐控件权威文档**） |
| 产线/运维 | 插拔全流程图 | [插拔全流程图.html](插拔全流程图.html) | 插盘→AUTO/MANUAL→整任务四步→结果显示→拔出复位（含 PNG 版本 [插拔全流程图.png](插拔全流程图.png)） |
| 研发 | 软件规划书 | [UdiskTools软件规划书.md](UdiskTools软件规划书.md) | 需求、功能规格、验收标准、Phase 计划、技术方案、文档沿革 |
| 研发/UI | 控件位置对照图 | [控件位置对照图.html](控件位置对照图.html) | 主窗口/配置窗口控件名称、ID 与位置对照（浏览器打开） |
| 研发/新成员 | 开发流程 | [开发流程.md](开发流程.md) | 构建（make/CMake）、测试、CI 门禁、提交/分支规则、质量门禁 |
| 所有 | 变更记录 | [CHANGELOG.md](CHANGELOG.md) | 审计修复批次 + 功能优化（软件 v1.2.0）+ 文档结构变更主题记录 |
| 所有 | 代码审计报告（副本） | [UdiskTools-代码审计报告-20261024.md](UdiskTools-代码审计报告-20261024.md) | 2026-10-24 审计：发现/状态/证据/批次修复跟踪（唯一有效版本） |
| 历史 | 归档文档 | [archive/](archive/) | 旧版策划方案/规划书/GUI 模拟页（仅历史参考，已被现行文档取代） |

## 2. 快速入口

| 事项 | 入口 |
|---|---|
| 构建（Make） | 仓库根目录执行 `make`（产物 `bin/udisktools_v<软件版本>.exe`，版本号自动取自 version.h）；详见 [开发流程.md](开发流程.md) |
| 构建（CMake 备用） | 仓库根目录 `cmake -B build -G "MinGW Makefiles" && cmake --build build` |
| 默认配置一致性校验 | `pwsh scripts/gen_default_ini.ps1 -Check` 或 `mingw32-make check-default-ini` |
| 测试 | `pwsh tests/run_tests.ps1`（-CC gcc/clang 选择编译器；以 `tests/` 实际脚本与 CI 为准） |
| CLI 冒烟 | `bin\udisktools.exe --version`（软件版本 1.2.0 + 构建时间）/ `--check-ini --ini profiles\UdiskTools.ini`（机型数 + 配置文件版本） |
| CI | [.github/workflows/build.yml](../.github/workflows/build.yml)（构建 + 默认配置校验 + 测试 + CLI 冒烟） |
| 项目概览 | [../README.md](../README.md) |

## 3. 文档维护规则

| 变更类型 | 必须同步的文档/文件 |
|---|---|
| 改 ini 键名/语义/默认值 | [配置文件使用手册.md](配置文件使用手册.md)、[UdiskTools软件规划书.md](UdiskTools软件规划书.md)、`profiles/UdiskTools_default.ini`（内置默认，两处共用）、`src/default_ini.h`（用生成器重生成并过 CI 校验） |
| 改 GUI 控件/状态/热键/配置窗口行为 | [UI功能状态说明.md](UI功能状态说明.md)（逐控件权威）、`ui/resource.h`、[控件位置对照图.html](控件位置对照图.html)（如布局变化） |
| 改插拔/任务判定顺序 | [插拔全流程图.html](插拔全流程图.html)、[UdiskTools软件规划书.md](UdiskTools软件规划书.md) §4.2 |
| 改构建/测试/CI/提交规则 | [开发流程.md](开发流程.md)、[CHANGELOG.md](CHANGELOG.md)（涉及流程变更时） |
| 新增修复/功能批次 | [CHANGELOG.md](CHANGELOG.md)（主题级记录，不写未验证承诺） |
| 改版本号 | **软件版本**：`src/version.h`、`res/udisktools.rc`、README/手册/UI 说明/开发流程；**配置文件版本**：`profiles/UdiskTools.ini` `[TOOLSINFO] VERSION`（与软件版本独立，不随程序升级） |

## 4. 目录结构

```
docs/
├── README.md                          # 本索引
├── CHANGELOG.md                       # 变更记录（审计修复批次 + 功能优化 v1.2.0 + 文档结构变更）
├── 配置文件使用手册.md                 # 产线/运维：ini 全键说明与排错
├── 用户使用手册.md                     # 操作员：安装启动/典型操作/FAQ
├── UI功能状态说明.md                   # 逐控件权威参考（53 个 ID 全覆盖）
├── UdiskTools软件规划书.md             # 规划/需求/验收/Phase 计划
├── 开发流程.md                         # 构建/测试/CI/提交与版本规则
├── 插拔全流程图.html / .png           # 全流程条件判断图
├── 控件位置对照图.html                 # 控件 ID 与位置对照（浏览器打开）
├── UdiskTools-代码审计报告-20261024.md # 审计报告（唯一有效版本）
└── archive/                           # 历史归档（保持原样，不作现行依据）
    ├── UdiskTools软件规划书_v1.0(2026-10-21).md
    ├── 策划方案-UdiskOps可移动磁盘操作软件_v1.0(2026-10-20).md
    ├── 策划方案-UdiskTools合并重构(C语言版)_v2.0(2026-10-20).md
    └── gui-mockup/index.html
```

> 项目规模较小，`docs/` 采用扁平结构：现行文档全部在本层，仅 `archive/` 一个历史子目录。
> 临时调试目录 `temp_run/`（工作区根目录、仓库外）：不纳入构建/测试清理，禁止删除或改动。
