# ============================================================
# FindFFmpeg.cmake — 跨平台 FFmpeg 查找模块
#
# 策略：
#   1. 项目内置 libs/ffmpeg/ — 直接用（Windows 开发环境）
#   2. FFMPEG_ROOT — 用户指定路径
#   3. pkg-config — Linux / macOS Homebrew
#   4. find_path / find_library — 标准系统查找
#
# 导出 IMPORTED 目标：FFmpeg::avcodec FFmpeg::avformat ...
# ============================================================

include(FindPackageHandleStandardArgs)

set(FFMPEG_COMPONENTS avcodec avformat avutil swscale swresample avfilter)

# ---- 步骤 1: 确定 FFmpeg 根目录 ----
set(_FFMPEG_ROOT)

# 1a) 项目内置（Windows MinGW 免安装环境）
set(_PROJ_FFMPEG "${CMAKE_CURRENT_LIST_DIR}/../libs/ffmpeg")
if(EXISTS "${_PROJ_FFMPEG}/include/libavcodec/avcodec.h")
    set(_FFMPEG_ROOT "${_PROJ_FFMPEG}")
    message(STATUS "FFmpeg: using project-bundled at ${_FFMPEG_ROOT}")
endif()

# 1b) FFMPEG_ROOT 变量
if(NOT _FFMPEG_ROOT AND DEFINED FFMPEG_ROOT AND FFMPEG_ROOT)
    if(EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
        set(_FFMPEG_ROOT "${FFMPEG_ROOT}")
    endif()
endif()

# 1c) 系统路径
if(NOT _FFMPEG_ROOT)
    foreach(prefix /usr /usr/local /opt/homebrew /opt/local)
        if(EXISTS "${prefix}/include/libavcodec/avcodec.h")
            set(_FFMPEG_ROOT "${prefix}")
            message(STATUS "FFmpeg: found at ${_FFMPEG_ROOT}")
            break()
        endif()
    endforeach()
endif()

# 1d) pkg-config
if(NOT _FFMPEG_ROOT)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(PC_avcodec QUIET libavcodec)
        if(PC_avcodec_FOUND AND PC_avcodec_INCLUDEDIR)
            get_filename_component(_FFMPEG_ROOT "${PC_avcodec_INCLUDEDIR}" DIRECTORY)
        endif()
    endif()
endif()

# ---- 步骤 2: 逐个组件定位库文件 ----
set(FFMPEG_INCLUDE_DIRS "${_FFMPEG_ROOT}/include")
set(FFMPEG_LIBRARY_DIRS)
set(FFMPEG_FOUND_COMPONENTS)

foreach(comp ${FFMPEG_COMPONENTS})
    set(FFMPEG_${comp}_INCLUDE_DIR "${_FFMPEG_ROOT}/include")

    # 库文件：按后缀优先级查找
    set(_found FALSE)
    foreach(libdir "${_FFMPEG_ROOT}/lib" "${_FFMPEG_ROOT}/lib64" "${_FFMPEG_ROOT}/lib/x86_64-linux-gnu")
        foreach(suffix ".dll.a" ".so" ".dylib" ".lib" ".a")
            set(_fullpath "${libdir}/lib${comp}${suffix}")
            if(EXISTS "${_fullpath}")
                set(FFMPEG_${comp}_LIBRARY "${_fullpath}")
                set(_found TRUE)
                break()
            endif()
            set(_fullpath "${libdir}/${comp}${suffix}")
            if(EXISTS "${_fullpath}")
                set(FFMPEG_${comp}_LIBRARY "${_fullpath}")
                set(_found TRUE)
                break()
            endif()
        endforeach()
        if(_found)
            break()
        endif()
    endforeach()

    if(NOT _found AND PC_${comp}_FOUND)
        set(FFMPEG_${comp}_LIBRARY ${PC_${comp}_LIBRARIES})
        set(_found TRUE)
    endif()

    if(NOT _found AND _FFMPEG_ROOT)
        find_library(FFMPEG_${comp}_LIBRARY
            NAMES ${comp} lib${comp}
            PATHS "${_FFMPEG_ROOT}"
            PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
            NO_DEFAULT_PATH
        )
    endif()

    if(FFMPEG_${comp}_LIBRARY)
        list(APPEND FFMPEG_FOUND_COMPONENTS ${comp})
        set(FFmpeg_${comp}_FOUND TRUE)
        get_filename_component(_ld "${FFMPEG_${comp}_LIBRARY}" DIRECTORY)
        list(APPEND FFMPEG_LIBRARY_DIRS "${_ld}")
        message(STATUS "  ${comp}: ${FFMPEG_${comp}_LIBRARY}")
    else()
        set(FFmpeg_${comp}_FOUND FALSE)
        message(STATUS "  ${comp}: NOT FOUND")
    endif()
endforeach()

if(FFMPEG_LIBRARY_DIRS)
    list(REMOVE_DUPLICATES FFMPEG_LIBRARY_DIRS)
endif()

# ---- 步骤 3: 标准检查 ----
find_package_handle_standard_args(FFmpeg
    REQUIRED_VARS _FFMPEG_ROOT FFMPEG_avcodec_LIBRARY
    HANDLE_COMPONENTS
)

# ---- 步骤 4: 创建 IMPORTED 目标 ----
if(FFmpeg_FOUND AND NOT TARGET FFmpeg::avcodec)
    foreach(comp ${FFMPEG_COMPONENTS})
        if(FFMPEG_${comp}_LIBRARY)
            add_library(FFmpeg::${comp} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${comp} PROPERTIES
                IMPORTED_LOCATION "${FFMPEG_${comp}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_INCLUDE_DIRS}"
            )
        endif()
    endforeach()
endif()
