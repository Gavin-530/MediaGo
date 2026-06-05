// MediaGo — 转码配置结构（v2）
// 用户显式指定所有参数，不做自主推断/自动决策
// 保证处理管线与输出结果满足行业标准

#pragma once

#include <cstdint>

// ============================================================
// 视频配置
// ============================================================
struct VideoConfig {
    // 编码器名："libx264", "hevc_nvenc", "prores_ks" 等
    // nullptr 或 "copy" = 流拷贝，不重编码
    const char* codec = nullptr;

    // 质量：CRF 模式（-1=编码器默认）
    int crf = -1;

    // 码率控制（0=不设置，与 CRF 互斥）
    int64_t bitrate = 0;

    // 最大码率（0=不设置）
    int64_t maxrate = 0;

    // 缓冲大小（0=不设置）
    int64_t bufsize = 0;

    // 编码器预设："ultrafast" "fast" "medium" "slow" 等
    const char* preset = nullptr;

    // 编码器调优："film" "animation" "grain" 等
    const char* tune = nullptr;

    // 编码器 profile："baseline" "main" "high" 等
    const char* profile = nullptr;

    // 缩放
    int width = 0;                   // 目标宽度，0=保持
    int height = 0;                  // 目标高度，0=保持
    bool keep_aspect = true;         // 保持宽高比

    // 帧率（0=保持源帧率，仅编码时有效）
    double fps = 0.0;

    // 像素格式
    const char* pixel_fmt = nullptr;

    // 关键帧间隔（GOP 大小，0=编码器默认）
    int gop_size = 0;

    // 线程数（0=自动）
    int threads = 0;

    // 自定义滤镜图（为空则不添加额外滤镜，仅在编码路径有效）
    const char* filters = nullptr;
};

// ============================================================
// 音频配置
// ============================================================
struct AudioConfig {
    // 编码器名："aac", "libmp3lame", "flac", "opus" 等
    // nullptr 或 "copy" = 流拷贝
    const char* codec = nullptr;

    // 码率（0=编码器默认）
    int64_t bitrate = 0;

    // 采样率（0=保持源采样率）
    int sample_rate = 0;

    // 声道布局（nullptr=保持）
    const char* channel_layout = nullptr;
};

// ============================================================
// 转码任务配置
// ============================================================
struct TranscodeConfig {
    const char* input = nullptr;
    const char* output = nullptr;

    VideoConfig video;
    AudioConfig audio;

    // 容器格式名（nullptr=自动从扩展名推断）
    const char* format = nullptr;

    // 是否覆盖已存在的输出文件
    bool overwrite = false;

    // 元数据携带（ICC Profile / 色彩空间等）
    bool preserve_metadata = true;
};
