# ============================================================
# MediaGo — 开发工具（FFmpeg 环境诊断，非发布产品）
# ============================================================

# ---- 诊断程序（开发/测试用）----
add_executable(diag
    diag_main.cpp
    diag.cpp
)

target_include_directories(diag PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/libs"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(diag PRIVATE
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::avfilter
)

if(WIN32)
    target_link_libraries(diag PRIVATE
        bcrypt ole32 ws2_32 secur32 mfuuid strmiids uuid
    )
endif()

if(NOT MSVC)
    target_compile_options(diag PRIVATE -Wno-deprecated-declarations)
endif()

# Windows: 拷贝 FFmpeg DLL
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
        add_custom_command(TARGET diag POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:diag>"
            COMMENT "Copying FFmpeg DLL for diag: ${_dll_name}"
        )
    endforeach()
endif()
