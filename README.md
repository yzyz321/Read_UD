# UdiskTools

Windows 可移动磁盘（U 盘）自动化操作工具。C11 + Win32 API 全新开发，单 exe 免运行库。

一个 exe + 一个 ini：按机型配置写入、读取判定、格式化、USB 通讯模式识别、标签读写、按台标识生成等操作，支持自动（插盘触发）/ 手动（按钮、热键）两种方式，全程日志留痕。

详细规划见 [docs/软件规划书.md](docs/软件规划书.md)，交互式界面参考见 [docs/mockup/index.html](docs/mockup/index.html)（浏览器直接打开）。

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
├── src/            # 源码（main / ui / device / ops / ini / pattern / rule / log / md5 / util）
├── ui/             # resource.h（控件 ID 登记）
├── profiles/       # 初始参考配置 UdiskTools.ini（4 机型）
├── docs/           # 规划书、GUI 模拟页
├── tests/          # 测试
├── scripts/        # 辅助脚本
└── .github/        # CI（GitHub Actions）
```

## 分支与协作

见 [docs/开发流程.md](docs/开发流程.md)。
