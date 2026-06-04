// MediaGo - 配置模块
//
// 核心原则：所选即所得（用户指定则严格执行），不选则保持原始
// 所有可选参数以 int=-1 / const char*=nullptr 表示"未指定"
//
// 数据结构说明：
//   - 视频/音频/转码配置 → transcode_config.h
//   - 缩放/编解码器枚举 → 本文件
//   - FFmpeg 运行时枚举 → 本文件 (config_list_codecs / config_list_pixel_fmts)

#pragma once

#include <cstdint>
#include <cstdio>

// ---- 缩放 ----

// 缩放模式（仅当 scale_w 或 scale_h > 0 时生效）
enum class ScaleMode {
    NONE = 0,    // 不缩放
    FIT = 1,     // 等比缩放，全部内容可见，可能留黑边（默认）
    FILL = 2,    // 等比缩放，填满目标区域，可能裁剪
    STRETCH = 3, // 拉伸填充，无视宽高比
};

// 缩放算法（对应 sws_scale 的 flags）
enum class ScaleAlgorithm {
    FAST_BILINEAR = 1,  // 双线性，速度快但细节模糊，适合实时预览
    BILINEAR = 2,       // 双线性（精度稍好于 FAST_BILINEAR）
    BICUBIC = 4,        // 双三次，折中选择，质量/性能平衡
    BOX = 8,            // 盒式，适合缩小
    GAUSS = 16,         // 高斯，柔和
    SINC = 32,          // Sinc 函数，理论最优但可能振铃
    LANCZOS = 64,       // Lanczos，图像缩放的行业标准，锐利清晰（推荐）
    SPLINE = 128,       // 样条插值
    AREA = 512,         // 区域平均，适合大幅缩小（如缩略图）
};

// ---- FFmpeg 环境枚举（供 UI/CLI 展示可用选项）----

// 单条编解码器信息
struct CodecInfo {
    const char* name;       // 编码器名称（如 "libx264"）
    const char* long_name;  // 人类可读名称（如 "libx264 H.264 / AVC / MPEG-4 AVC"）
    const char* type;       // "video" / "audio" / "subtitle"
    bool is_encoder;
    bool is_decoder;
    bool is_hardware;
};

// 像素格式信息
struct PixelFmtInfo {
    const char* name;          // 如 "yuv420p"
    int bits_per_pixel;        // 每像素总位数
    int log2_chroma_w;         // 色度水平子采样（4:2:0 → 1; 4:4:4 → 0）
    int log2_chroma_h;         // 色度垂直子采样
};

// 枚举所有可用的编解码器
// max_count: 最大返回数量；返回实际数量
int config_list_codecs(CodecInfo* out, int max_count,
                       bool encoders_only, bool video_only);

// 枚举所有可用的像素格式
int config_list_pixel_fmts(PixelFmtInfo* out, int max_count);
