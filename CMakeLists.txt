cmake_minimum_required(VERSION 3.15)
project(UdiskTools C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "UdiskTools 仅支持 Windows（Win32 API）")
endif()

# 全程 Unicode（W 系列 API）
add_compile_definitions(UNICODE _UNICODE)

# 警告从紧
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

set(SRC
    src/main.c
)

add_executable(udisktools WIN32 ${SRC})
target_link_libraries(udisktools PRIVATE user32 gdi32 shell32 comctl32)

# wWinMain 入口（MinGW/Clang 需要 -municode）
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(udisktools PRIVATE -municode)
endif()

# Phase 1 起逐步加入：device.c ops.c ini.c pattern.c rule.c log.c md5.c util.c
