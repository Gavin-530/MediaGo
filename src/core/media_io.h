// MediaGo - 统一媒体 I/O 接口
// 光栅图通过 FFmpeg codec 处理（PNG/JPEG/BMP/WebP/AVIF/HEIF 等 30+ 格式）
// 矢量图通过 nanosvg 处理（SVG 解析 + 光栅化）
// 所有内存统一走 av_malloc/av_free，杜绝分配器混用
//
// 重构要点（v2）：
//   - 解码目标格式从硬编码 RGBA8 改为由 Config 配置
//   - 编码从 media_save_png/jpg 两个函数合并为统一的 media_encode
//   - 新增 media_probe：获取源文件属性，供配置回填和决策引擎使用
//   - 新增 media_stream_copy：bit-exact 流拷贝（同格式无损直通）
//   - SVG 光栅化接受 ScaleConfig 而非硬编码 w/h

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
// 解码
// ============================================================

// 解码媒体文件到指定像素格式的帧
//   path     - 输入文件路径
//   dst_fmt  - 目标像素格式；AV_PIX_FMT_NONE 表示与源格式一致
//   frame_out- 输出的 AVFrame（由调用方 av_frame_free 释放）
// 失败返回 false
//
// 说明：dst_fmt=AV_PIX_FMT_NONE 时，直接输出解码器原始格式，
// 不经过 sws_scale 转换，保证位深和色彩空间完全保留。
bool media_decode(const char* path, AVPixelFormat dst_fmt,
                  AVFrame** frame_out);

// ============================================================
// 编码
// ============================================================

// 统一编码接口：将 AVFrame 编码为指定格式文件
//   path      - 输出文件路径（扩展名决定容器格式）
//   frame     - 待编码的帧（像素格式由帧自身决定）
//   cfg       - 图片编码配置（编码器、质量等；未指定字段从 frame 属性回填）
//
// 说明：
//   - cfg.encode.name 为 nullptr 时，根据输出扩展名自动选择编码器
//   - cfg.encode.quality 为 -1 时，按编码器默认质量
//   - 编码器选择的像素格式优先取 cfg.encode.pixel_fmt，其次取 frame 自身格式
bool media_encode(const char* path, const AVFrame* frame,
                  const ImageConfig& cfg);

// ============================================================
// 流拷贝（bit-exact，同格式无损直通）
// ============================================================

// 容器/编码相同的格式直接复制 bitstream，零质量损失
// 适用于：PNG→PNG, JPEG→JPEG（同参数）, 或任何仅需重封装不需重编码的场景
// 内部通过 avformat demux → remux 实现
bool media_stream_copy(const char* input, const char* output);

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
