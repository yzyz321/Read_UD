# UdiskTools 简易 Makefile（无 CMake 环境时使用）
# 依赖：MinGW-w64 gcc 或兼容的 clang（目标 x86_64-w64-windows-gnu）
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -DUNICODE -D_UNICODE -municode
LDFLAGS := -mwindows -luser32 -lgdi32 -lshell32 -lcomctl32
SRC     := src/main.c
BIN     := bin/udisktools.exe

.PHONY: all clean run

all: $(BIN)

$(BIN): $(SRC) | bin
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

bin:
	mkdir -p bin

run: $(BIN)
	./$(BIN)

clean:
	rm -rf bin
