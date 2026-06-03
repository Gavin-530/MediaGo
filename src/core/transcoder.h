// MediaGo - 转码引擎
// 统一流水线：探测 → 配置解析 → 决策(COPY/ENCODE) → 执行
//
// 重构要点（v2）：
//   - convert_image 和 transcode_media 合并为统一的 process_media
//   - 流水线决策由 Strategy 和源目标格式兼容性共同决定
//   - COPY 路径走 media_stream_copy（bit-exact）
//   - ENCODE 路径走 media_decode → media_encode（可控参数）
//   - 所有未指定参数从源文件属性回填

#pragma once

#include "config.h"

// 处理结果
struct TranscodeResult {
    bool ok;
    const char* error;  // 失败时的静态描述
};

// 处理报告（成功时返回实际执行了哪种路径和参数）
struct ProcessReport {
    bool ok;
    const char* error;

    // 实际采用的策略
    bool used_copy;       // true = 走了 stream copy；false = decode → encode

    // 源文件属性（供调用方参考）
    char src_codec[64];
    int  src_width;
    int  src_height;
    char src_pix_fmt[32];
    int  src_bit_depth;

    // 输出属性
    char out_codec[64];
    int  out_width;
    int  out_height;
    char out_pix_fmt[32];
};

// ============================================================
// 统一处理流水线（图片 + 视频 remux）
// ============================================================

// 统一媒体处理入口
//   1. media_probe 获取源属性
//   2. 回填 cfg 中未指定的参数（从源属性采样）
//   3. 决策：
//      - Strategy::COPY → media_stream_copy（bit-exact）
//      - Strategy::ENCODE → media_decode + media_encode
//      - Strategy::AUTO → 同编码+同容器 → COPY；否则 → ENCODE
//   4. 执行并返回报告
//
// cfg: 图片配置（视频配置 Phase 3 补充）
// report: 可选，传入非 nullptr 则填充处理报告
TranscodeResult process_media(const char* input, const char* output,
                              const ImageConfig& cfg,
                              ProcessReport* report = nullptr);

// ============================================================
// 向后兼容接口（不推荐使用，后续移除）
// ============================================================

// 图片格式转换（等价于 process_media + AUTO 策略）
TranscodeResult convert_image(const char* input, const char* output);

// 容器转封装/remux（等价于 process_media + COPY 策略）
TranscodeResult transcode_media(const char* input, const char* output);
