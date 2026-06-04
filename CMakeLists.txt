# ============================================================
# MediaGo — 跨平台专业媒体处理工具
# 构建: cmake -B build && cmake --build build
# ============================================================

cmake_minimum_required(VERSION 3.16)
project(MediaGo
    VERSION 1.0.0
    DESCRIPTION "跨平台专业媒体处理工具，基于 FFmpeg + nanosvg"
    LANGUAGES C CXX
)

# ============================================================
# 全局设置
# ============================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# 跨平台编译选项
if(MSVC)
    add_compile_options(/W4 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ============================================================
# CMake 模块路径
# ============================================================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ============================================================
# 生成 compile_commands.json（供 IDE / clangd 使用）
# ============================================================

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================
# 依赖: FFmpeg
# ============================================================

find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale swresample avfilter)

# 打印 FFmpeg 信息
message(STATUS "FFmpeg include: ${FFMPEG_INCLUDE_DIRS}")
foreach(comp avcodec avformat avutil swscale swresample avfilter)
    message(STATUS "  ${comp}: ${FFMPEG_${comp}_LIBRARY}")
endforeach()

# ============================================================
# 依赖: 内嵌第三方库（纯头文件）
# ============================================================

# nlohmann/json — JSON 解析
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE "${CMAKE_SOURCE_DIR}/libs/nlohmann")

# nanosvg — SVG 解析与光栅化
add_library(nanosvg INTERFACE)
target_include_directories(nanosvg INTERFACE "${CMAKE_SOURCE_DIR}/libs/nanosvg")

# ============================================================
# 主程序
# ============================================================

set(APP_SOURCES
    src/main.cpp
    src/core/config.cpp
    src/core/media_io.cpp
    src/core/transcode_engine.cpp
    src/core/batch.cpp
)

add_executable(MediaGo ${APP_SOURCES})

# 包含路径
target_include_directories(MediaGo PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/libs"
)

# 链接
target_link_libraries(MediaGo PRIVATE
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swscale
    FFmpeg::swresample
    FFmpeg::avfilter
    nlohmann_json
    nanosvg
)

# Windows 系统库
if(WIN32)
    target_link_libraries(MediaGo PRIVATE
        bcrypt ole32 ws2_32 secur32 mfuuid strmiids uuid
    )
endif()

# macOS 系统库
if(APPLE)
    target_link_libraries(MediaGo PRIVATE
        "-framework CoreFoundation"
        "-framework CoreVideo"
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework AudioToolbox"
    )
endif()

# Linux: 链接 pthread / dl 等（通常 FFmpeg 已包含，显式添加确保完整）
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(MediaGo PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

# Windows: 拷贝 FFmpeg DLL 到输出目录
if(WIN32 AND FFMPEG_LIBRARY_DIRS)
    list(GET FFMPEG_LIBRARY_DIRS 0 _FFMPEG_LIB_DIR)
    if(EXISTS "${_FFMPEG_LIB_DIR}/../bin")
        set(_FFMPEG_BIN_DIR "${_FFMPEG_LIB_DIR}/../bin")
    else()
        set(_FFMPEG_BIN_DIR "${_FFMPEG_LIB_DIR}")
    endif()

    file(GLOB _FFMPEG_DLLS "${_FFMPEG_BIN_DIR}/*.dll")
    foreach(_dll ${_FFMPEG_DLLS})
        get_filename_component(_dll_name "${_dll}" NAME)
        add_custom_command(TARGET MediaGo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:MediaGo>"
            COMMENT "Copying FFmpeg DLL: ${_dll_name}"
        )
    endforeach()
endif()

# 编译选项
if(NOT MSVC)
    target_compile_options(MediaGo PRIVATE
        -Wno-deprecated-declarations
    )
endif()

# ============================================================
# 开发工具模块（单元测试 + 诊断，非发布产品）
# ============================================================

option(BUILD_TESTING "构建开发工具" ON)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(devtools)
endif()
