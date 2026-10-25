cmake_minimum_required(VERSION 3.15)
project(UdiskTools C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "UdiskTools 仅支持 Windows（Win32 API）")
endif()

# 资源脚本（图标/版本信息/manifest）需要 RC 语言
enable_language(RC)

# 全程 Unicode（W 系列 API）+ 高 DPI 声明所需版本
add_compile_definitions(UNICODE _UNICODE _WIN32_WINNT=0x0605)

# 警告从紧
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
    # windres 需要能找到 version.h 与 ico/manifest（rc 内引用相对 res/）；
    # --codepage=65001：.rc 为 UTF-8（FileDescription 含中文）
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=65001 -I${CMAKE_CURRENT_SOURCE_DIR}/src -I${CMAKE_CURRENT_SOURCE_DIR}/res")
endif()

set(SRC
    src/main.c
    src/gui.c
    src/gui_config.c
    src/ops.c
    src/device.c
    src/config.c
    src/pattern.c
    src/util.c
    src/md5.c
    src/usbspeed.c
    res/udisktools.rc
)

add_executable(udisktools WIN32 ${SRC})
target_include_directories(udisktools PRIVATE ui src)
# dbghelp：main.c 崩溃转储（MiniDumpWriteDump）
target_link_libraries(udisktools PRIVATE user32 gdi32 shell32 comctl32 advapi32 setupapi cfgmgr32 dbghelp)

# 产物名带软件版本号：udisktools_v<APP_VERSION>.exe（与 Makefile 一致）
file(STRINGS src/version.h APP_VER_LINE REGEX "^#define APP_VERSION[ \t]+\"")
string(REGEX REPLACE "^#define APP_VERSION[ \t]+\"([^\"]+)\".*" "\\1" APP_VERSION "${APP_VER_LINE}")
set_target_properties(udisktools PROPERTIES OUTPUT_NAME "udisktools_v${APP_VERSION}")

# wWinMain 入口（MinGW/Clang 需要 -municode）
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(udisktools PRIVATE -municode)
