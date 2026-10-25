# UdiskTools 测试

本目录是 UdiskTools 的自动化测试（纯 C 可执行程序 + PowerShell 构建/汇总脚本），
不依赖任何第三方测试框架；所有测试均可在无 U 盘、无 GUI 交互的环境下运行。

## 目录结构

```
tests/
├── README.md                 本文档
├── run_tests.ps1             一键构建 + 运行 + 汇总（-CC gcc|clang、-Filter）
├── unit/                     单元测试
│   ├── test_pattern.c        通配符匹配：* / ? 回溯、大小写、捕获、截断、NULL
│   ├── test_config.c         ini 解析/单键写回：注释(;)、编码/BOM、行尾、原子写、回读校验
│   └── test_encoding.c       统一编码探测：BOM/无 BOM UTF-16/严格 UTF-8/损坏 UTF-8/GBK、
│                             read_file_text、解析健壮性、config API 防御、UTF-16 保存保编码
├── integration/              集成测试
│   ├── test_ops.c            op_* 结果码/op_write P3-3 目标目录创建时机/op_read 判定与
│   │                         ${MATCH_1} 截断 WARN/GUI 消息语义（隐藏窗口）
│   ├── test_device_msg.c     dev_post_changes 设备插拔消息协议（位图/顺序/NULL）
│   ├── test_gui_state.c      GUI 状态函数（标题/进度/复位/设备行/最小尺寸/STATIC 色/热键）
│   └── test_logging.c        日志并发/轮转/alt 兜底/shutdown、post_owned_string 所有权、
│                             轮询线程 start-stop、卷信息、exe 资源(图标/manifest/版本)
└── fixtures/                 预留；当前测试均在 %TEMP% 内自建夹具，无需静态文件
```

## 依赖

- Windows 10/11（Win32 API；部分用例读取系统盘卷信息/注册热键）
- PowerShell 7（`pwsh`）
- MinGW-w64 工具链（`gcc` 或 `clang`、`windres`）；已在 llvm-mingw + UCRT 验证
- `-CC` 指定的编译器必须在 PATH 中；`windres` 必须可用（资源/主程序构建）

## 运行

在仓库根目录（或任意目录）执行：

```powershell
# 默认 gcc
pwsh -NoProfile -File tests/run_tests.ps1

# 指定 clang
pwsh -NoProfile -File tests/run_tests.ps1 -CC clang

# 只跑名称包含 pattern 的测试
pwsh -NoProfile -File tests/run_tests.ps1 -Filter pattern
```

或通过 Makefile：

```powershell
mingw32-make test             # 用 CC 变量（默认 gcc）
mingw32-make CC=clang test    # 用 clang
mingw32-make test-clang
```

脚本行为：

1. 清理并重建 `tests/build/`（已在 `.gitignore` 中忽略）；
2. 用统一 flags（`-std=c11 -Wall -Wextra -Werror -DUNICODE -D_UNICODE
   -D_WIN32_WINNT=0x0605 -Iui -Isrc`）构建每个测试，并构建主程序
   （`windres` 资源 + 全部 `src/*.c`，`-municode -mwindows -ldbghelp`）；
3. 以 **repo 根为工作目录** 运行每个测试（因此测试可用 `profiles/UdiskTools.ini`
   等相对路径），每个进程 **30s 超时守护**，超时/非 0 退出即判失败；
4. 运行 CLI 用例：`--version`、`--help`、`--check-ini --ini profiles/UdiskTools.ini`
   （exit 0）、`--check-ini --ini <不存在>`（exit 1 + 关键输出校验）；
5. 输出 `Name/Status/耗时/摘要` 表格；全部通过 `exit 0`，否则 `exit 1`。
   失败时打印构建命令与输出尾部，完整日志见 `tests/build/`。

CI 等价命令（GitHub Actions 已在 Build 步骤后调用）：

```yaml
- name: Run tests
  shell: pwsh
  run: |
    $env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
    ./tests/run_tests.ps1 -CC gcc
```

## 写盘与清理约定

- 测试只写 `%TEMP%` 下由自身创建的 `udk_tests_*` 目录，并在退出前清理；
- `test_logging.c` 验证日志缓存/轮转，日志落在测试 exe 同目录 `tests/build/logs/`
  （构建目录，gitignored）；
- 无 U 盘 / 缺少某盘符 / 未构建主程序时，相关断言输出 `SKIP` 而非硬失败；
- `tests/build/` 可随时删除：`Remove-Item tests/build -Recurse -Force`。

## 覆盖要点（与审计修复项对应）

| 修复项 | 测试 |
|---|---|
| P1-1/P1-7 cfg_save_key 崩溃/CR 累积/编码/BOM/注释/5a 分号链 | `unit/test_config.c` |
| P1-2/P2-14 统一编码探测、损坏 UTF-8 拒绝、二进制拒绝 | `unit/test_encoding.c` |
| P1-9 pattern `*` 回溯/大小写/捕获 | `unit/test_pattern.c` |
| P2-13 解析健壮性（容量/截断/畸形节/重复节/PRJNAME） | `unit/test_encoding.c` |
| P2-16 `${MATCH_1}` 捕获截断显式上报 | `unit/test_pattern.c`、`integration/test_ops.c` |
| P2-9 PostMessage 堆串所有权 | `integration/test_logging.c` |
| P2-17/P2-23 日志缓存/轮转/alt 兜底/卷信息免 SetupAPI | `integration/test_logging.c` |
| P1-14/P2-23 轮询线程停止/Join | `integration/test_logging.c` |
| P3-3 op_write 先匹配后建目录、结果码语义 | `integration/test_ops.c` |
| 设备事件协议（位图/同周期插拔） | `integration/test_device_msg.c` |
| GUI 状态：标题/进度/复位/设备行/最小尺寸/热键 | `integration/test_gui_state.c` |
| CLI（版本/帮助/配置校验退出码） | `run_tests.ps1` 内置 CLI 用例 |

## 已知限制

- 真实 U 盘插拔、RAW 盘、写保护/满盘等场景无法在无硬件 CI 上复现；相关代码路径由
  单元/集成测试覆盖接口层，物理场景仍需现场回归；
- `test_gui_state.c` 通过 `#include "gui.c"` 直接测试其 static 函数，不使用真实消息循环；
- 热键注册用例要求当前会话没有其它程序占用 `Ctrl+Alt+T/C`，冲突时该用例失败（属真实环境问题）。
