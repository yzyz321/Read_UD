# UdiskTools Makefile（MinGW-w64 gcc / llvm-mingw）
# 用法：make          构建到 bin/udisktools.exe
#       make run      构建并运行
#       make clean    清理
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0605
CFLAGS  += -Iui -Isrc
LDFLAGS := -municode -mwindows -luser32 -lgdi32 -lshell32 -lcomctl32 -ladvapi32
SRC     := src/main.c src/gui.c src/ops.c src/device.c src/config.c \
           src/pattern.c src/util.c src/md5.c
BIN     := bin/udisktools.exe

.PHONY: all clean run

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf bin
