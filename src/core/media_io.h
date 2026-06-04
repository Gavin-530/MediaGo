// MediaGo - 统一媒体 I/O 接口
// 探测源文件属性 + SVG 矢量图光栅化
// 所有内存统一走 av_malloc/av_free，杜绝分配器混用

#pragma once

#include "config.h"
#include <cstdint>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

// ============================================================
// 源文件属性（由 media_probe 返回）
// ============================================================

// 描述一个媒体文件的完整属性，供配置回填和决策引擎参考
struct SourceInfo {
    // ---- 基本属性 ----
    char codec_name[64];       // 编解码器名称（如 "png", "mjpeg", "hevc"）
    int codec_id;              // AVCodecID 枚举值
    int width;                 // 像素宽度
    int height;                // 像素高度

    // ---- 像素格式 ----
    AVPixelFormat pix_fmt;     // 源像素格式（如 AV_PIX_FMT_YUVJ420P）
    char pix_fmt_name[32];     // 像素格式字符串（如 "yuvj420p"）
    int bit_depth;             // 位深（8 / 10 / 12 / 16）

    // ---- 色彩空间 ----
    int color_space;           // AVColorSpace（如 AVCOL_SPC_BT709）
    int color_range;           // AVColorRange（如 AVCOL_RANGE_JPEG）
    int color_primaries;       // AVColorPrimaries
    int color_trc;             // 传递函数（AVColorTransferCharacteristic）

    // ---- 容器 ----
    char container[32];        // 容器格式名（如 "png_pipe", "mjpeg"）

    // ---- 元数据 ----
    bool has_icc;              // 是否嵌入 ICC Profile
    bool has_alpha;            // 是否包含透明通道

    // ---- 流信息 ----
    int nb_streams;            // 总流数
    bool is_image;             // 是否为单帧图片（true）还是多帧视频（false）
};

// ============================================================
// 探测
// ============================================================

// 打开媒体文件并返回完整属性
// 不解码任何像素数据，仅读取头部和流信息
// 失败返回 false
bool media_probe(const char* path, SourceInfo* info);

// ============================================================
// 矢量图 (nanosvg)
// ============================================================

// 解析 SVG 文件并光栅化为 RGBA 像素
// 使用 ScaleConfig 控制输出尺寸和缩放模式
//   path      - SVG 文件路径
//   scale     - 缩放配置（mode=NONE 则使用原始 SVG 尺寸）
//   w_out/h_out - 输出宽高
// 成功返回 data 指针（用 media_free 释放），失败返回 nullptr
uint8_t* svg_rasterize_ex(const char* path, const ScaleMode mode,
                           int scale_w, int scale_h, int* w_out, int* h_out);

// ---- 便捷包装（向后兼容）----
uint8_t* svg_rasterize(const char* path, int w, int h);

// ---- 清理解码器——返回内存 ----
void media_free(uint8_t* data);
