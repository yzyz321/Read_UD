# UdiskTools Makefile（MinGW-w64 gcc / llvm-mingw）
# 用法：make          构建到 bin/udisktools_v<软件版本>.exe
#       make run      构建并运行
#       make test     构建并运行全部测试（默认 CC=gcc；可用 make CC=clang test）
#       make test-clang 用 clang 跑全部测试
#       make clean    清理（含资源对象）
CC       := gcc
WINDRES  := windres
CFLAGS   := -std=c11 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0605
CFLAGS   += -Iui -Isrc
# dbghelp：main.c 崩溃转储（MiniDumpWriteDump）
LDFLAGS  := -municode -mwindows -luser32 -lgdi32 -lshell32 -lcomctl32 -ladvapi32 -lsetupapi -lcfgmgr32 -ldbghelp
SRC      := src/main.c src/gui.c src/gui_config.c src/ops.c src/device.c src/config.c \
            src/pattern.c src/util.c src/md5.c src/usbspeed.c
# 软件版本从 version.h 提取（"#define APP_VERSION   "X.Y.Z""），产物名带版本号
APP_VERSION := $(shell sed -n 's/^#define APP_VERSION  *"\([^"]*\)".*/\1/p' src/version.h)
RES_RC   := res/udisktools.rc
RES_OBJ  := bin/udisktools_res.o
BIN      := bin/udisktools_v$(APP_VERSION).exe

.PHONY: all clean run check-default-ini test test-clang

all: $(BIN)

$(BIN): $(SRC) $(RES_OBJ) | bin
	$(CC) $(CFLAGS) -o $@ $(SRC) $(RES_OBJ) $(LDFLAGS)

# 资源：图标(IDI_APP_ICON=101) + VERSIONINFO + 应用程序 manifest
# --codepage=65001：.rc 为 UTF-8（FileDescription 含中文），需显式告知 windres
$(RES_OBJ): $(RES_RC) res/udisktools.ico res/udisktools.manifest src/version.h | bin
	$(WINDRES) --codepage=65001 -I res -I src -o $@ $(RES_RC)

# 兼容 cmd 与 MSYS sh：`mkdir -p bin` 在 cmd 下会创建名为 "-p" 的目录，故显式调用 cmd。
bin:
	cmd /c "if not exist bin mkdir bin"

run: $(BIN)
	./$(BIN)

# 一键测试：tests/run_tests.ps1 负责构建测试程序与主程序、执行并汇总（失败返回非 0）
test:
	pwsh -NoProfile -File tests/run_tests.ps1 -CC $(CC)

test-clang:
	pwsh -NoProfile -File tests/run_tests.ps1 -CC clang

check-default-ini:
	pwsh -NoProfile -File scripts/gen_default_ini.ps1 -Check

clean:
	cmd /c "if exist bin rmdir /s /q bin"
