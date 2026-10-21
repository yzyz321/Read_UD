cmake_minimum_required(VERSION 3.15)
project(UdiskTools C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "UdiskTools 仅支持 Windows（Win32 API）")
endif()

# 全程 Unicode（W 系列 API）+ 高 DPI 声明所需版本
add_compile_definitions(UNICODE _UNICODE _WIN32_WINNT=0x0605)

# 警告从紧
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

set(SRC
    src/main.c
    src/gui.c
    src/ops.c
    src/device.c
    src/config.c
    src/pattern.c
    src/util.c
    src/md5.c
)

add_executable(udisktools WIN32 ${SRC})
target_include_directories(udisktools PRIVATE ui src)
target_link_libraries(udisktools PRIVATE user32 gdi32 shell32 comctl32 advapi32)

# wWinMain 入口（MinGW/Clang 需要 -municode）
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(udisktools PRIVATE -municode)
endif()
