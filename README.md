# UdiskTools

Windows 可移动磁盘（U 盘）自动化操作工具。C11 + Win32 API 全新开发，单 exe 免运行库。

一个 exe + 一个 ini：按机型配置写入、读取判定、格式化、USB 通讯模式识别、标签读写、按台标识生成等操作，支持自动（插盘触发）/ 手动（按钮、热键）两种方式，全程日志留痕。

详细规划见 [docs/软件规划书.md](docs/软件规划书.md)，交互式界面参考见 [docs/mockup/index.html](docs/mockup/index.html)（浏览器直接打开）。

## 当前状态（v1.0.0）

✅ 已实现并通过编译（零警告）与启动冒烟测试：

| 模块 | 说明 |
|---|---|
| ini 配置解析 | UTF-8/UTF-16/ANSI 自动识别，双层机型映射（PRJNAME 内部键 / DISPLAYNAME 显示名），行内注释保留 |
| 设备轮询 | 1 秒轮询 DRIVE_REMOVABLE，插/拔事件驱动 GUI 与自动任务 |
| 操作引擎 | write（COPY 复制 + MD5 校验）/ read（通配符匹配 + READRULE 判定 + ${MATCH_1} 捕获）/ format（fmifs FormatEx + 确认弹窗）/ usbinfo（USB 速率查询，取不到标注未知）/ 标签读写（写入回读比对）/ rwtest（8MB 写读 MD5 比对 + 速率） |
| GUI | 全控件按 resource.h 命名，双 CMD 输出区（RichEdit 分级配色），手动/自动模式标记，出错徽标三处联动，窗口置顶，热键 T/C |
| 配置窗口 | 默认只读，密码解锁编辑（连错 3 次锁定），单键保存回写 ini（保留其余内容） |
| DPI | Per-Monitor V2 感知，字体/布局随 DPI 缩放，WM_DPICHANGED 动态重排 |
| 日志 | 强制输出 logs\\YYYYMMDD.log（UTF-8），与界面输出同步 |

可选 ini 键：`AUTORUN=1`（机型节内）启用插盘自动执行；`BTN_*=显示名` 按机型自定义按钮文字。

## 快速开始（开发）

```
# 依赖：MinGW-w64 (gcc) 或 MSYS2 环境；构建可用 CMake 或 Make
git clone <repo-url>
cd udisktools

# 方式一：CMake
cmake -B build -G "MinGW Makefiles"
cmake --build build

# 方式二：直接 Make
make

# 运行（需管理员外的普通权限；ini 与 exe 同目录）
build\udisktools.exe
```

## 目录结构

```
udisktools/
├── src/            # 源码（main / gui / ops / device / config / pattern / util / md5）
├── ui/             # resource.h（控件 ID 登记）
├── profiles/       # 初始参考配置 UdiskTools.ini（4 机型）
├── docs/           # 规划书、GUI 模拟页
├── tests/          # 测试
├── scripts/        # 辅助脚本
└── .github/        # CI（GitHub Actions）
```

## 分支与协作

见 [docs/开发流程.md](docs/开发流程.md)。
