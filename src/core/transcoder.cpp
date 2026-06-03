// MediaGo - 转码引擎实现
// 统一流水线：探测 → 配置解析 → 决策(COPY/ENCODE) → 执行
//
// 流水线逻辑：
//   1. media_probe()     — 获取源文件完整属性（编解码器、像素格式、位深、色彩空间）
//   2. resolve_config()  — 用户未指定的参数从源属性回填
//   3. decide()          — 根据 Strategy 和格式兼容性选择 COPY 或 ENCODE 路径
//   4. execute()         — 执行所选路径
//
// 核心原则：
//   - 所选即所得：用户显式指定的参数不修改，直接传入编解码器
//   - 不选则保持原始：未指定参数从源文件属性采样回填
//   - 数学级严谨：保留所有色彩元数据（位深、色彩空间、传递函数）

#include "transcoder.h"
#include "media_io.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cstdio>
#include <cstring>

// 本地副本——根据扩展名推断编码器（供 report 使用，与 media_io.cpp 中的一致）
static AVCodecID guess_codec_id_for_ext(const char* ext) {
    if (!ext) return AV_CODEC_ID_NONE;
    if (!_stricmp(ext, ".png"))  return AV_CODEC_ID_PNG;
    if (!_stricmp(ext, ".jpg") || !_stricmp(ext, ".jpeg")) return AV_CODEC_ID_MJPEG;
    if (!_stricmp(ext, ".webp")) return AV_CODEC_ID_WEBP;
    if (!_stricmp(ext, ".bmp"))  return AV_CODEC_ID_BMP;
    if (!_stricmp(ext, ".tiff") || !_stricmp(ext, ".tif")) return AV_CODEC_ID_TIFF;
    if (!_stricmp(ext, ".avif")) return AV_CODEC_ID_AV1;
    return AV_CODEC_ID_NONE;
}

// ============================================================
// 配置回填：将源属性填入未指定的 Config 字段
// ============================================================

// 将 SourceInfo 回填到 ImageConfig 中所有未指定的字段
// 原则：用户已设置的保持不动；未设置的从 src 提取
static ImageConfig resolve_config(const ImageConfig& cfg, const SourceInfo& src) {
    ImageConfig resolved = cfg;

    // 中间格式：未指定则与源像素格式一致
    if (!resolved.intermediate_fmt) {
        resolved.intermediate_fmt = src.pix_fmt_name[0]
            ? _strdup(src.pix_fmt_name) : nullptr;
    }

    // 编码器：未指定则尝试用源编码器同名编码器
    if (!resolved.encode.name && src.codec_name[0]) {
        // 查找同名编码器（如源是 "png" 解码器，找 "png" 编码器）
        const AVCodec* enc = avcodec_find_encoder_by_name(src.codec_name);
        if (enc)
            resolved.encode.name = _strdup(src.codec_name);
    }

    // 像素格式：未指定则用源像素格式名
    if (!resolved.encode.pixel_fmt && src.pix_fmt_name[0]) {
        resolved.encode.pixel_fmt = _strdup(src.pix_fmt_name);
    }

    // 位深传递（通过 intermediate_fmt 间接控制）
    // 如果 intermediate_fmt 和 encode.pixel_fmt 都未指定，默认保持源位深

    return resolved;
}

// ============================================================
// 决策引擎：判断是否可以走 COPY 路径
// ============================================================

// 判断源和目标格式兼容能走 stream copy
// 条件：同编码器 + 输出容器支持该编码
static bool can_stream_copy(const SourceInfo& src, const char* output) {
    // 1. 检查输出容器是否支持源编码
    const AVOutputFormat* ofmt = av_guess_format(nullptr, output, nullptr);
    if (!ofmt) return false;

    // 2. 对于单帧图片，大多数图片 muxer 支持任意编码（通过 codec_tag 协商）
    //    简化处理：源和目标扩展名相同 → 可 copy
    const char* out_ext = strrchr(output, '.');
    const char* in_ext_hint = nullptr;

    // 根据源容器名推断扩展名
    if (strstr(src.container, "png"))       in_ext_hint = ".png";
    else if (strstr(src.container, "jpeg")) in_ext_hint = ".jpg";
    else if (strstr(src.container, "mjpeg"))in_ext_hint = ".jpg";
    else if (strstr(src.container, "webp")) in_ext_hint = ".webp";
    else if (strstr(src.container, "bmp"))  in_ext_hint = ".bmp";

    if (out_ext && in_ext_hint && !_stricmp(out_ext, in_ext_hint))
        return true;

    // 3. 相同 codec_id
    // （跨容器时大概率仍然可 copy，FFmpeg 会自动处理 bitstream filter）
    return true; // 默认允许尝试，失败了再 fallback 到 encode
}

// ============================================================
// 统一处理流水线
// ============================================================

TranscodeResult process_media(const char* input, const char* output,
                               const ImageConfig& cfg,
                               ProcessReport* report) {
    if (!input || !output)
        return { false, "null path" };

    // ---- 1. 探测源文件属性 ----
    SourceInfo src;
    if (!media_probe(input, &src))
        return { false, "cannot probe input" };

    // ---- 2. 回填配置 ----
    // 用户已设置 → 不动；未设置 → 从 src 采样
    ImageConfig resolved = resolve_config(cfg, src);

    // ---- 3. 决策 ----
    bool use_copy = false;

    switch (resolved.strategy) {
        case Strategy::COPY:
            use_copy = true;
            break;
        case Strategy::ENCODE:
            use_copy = false;
            break;
        case Strategy::AUTO:
        default:
            // 同编码+同容器 → COPY；否则 → ENCODE
            use_copy = can_stream_copy(src, output);
            break;
    }

    // ---- 4. 执行 ----
    bool ok = false;

    if (use_copy) {
        // ---- COPY 路径：bit-exact 流拷贝 ----
        ok = media_stream_copy(input, output);

        if (!ok && resolved.strategy == Strategy::AUTO) {
            // stream copy 失败（如容器不兼容），自动降级到 encode
            use_copy = false;
        }
    }

    if (!use_copy) {
        // ---- ENCODE 路径：解码 → 中间帧 → 编码 ----

        // 4a. 确定中间像素格式
        AVPixelFormat inter_fmt = AV_PIX_FMT_NONE;  // NONE = 保留源格式
        if (resolved.intermediate_fmt) {
            inter_fmt = av_get_pix_fmt(resolved.intermediate_fmt);
            if (inter_fmt == AV_PIX_FMT_NONE)
                inter_fmt = src.pix_fmt;  // fallback to source fmt
        }

        // 4b. 解码
        AVFrame* frame = nullptr;
        if (!media_decode(input, inter_fmt, &frame)) {
            if (report) {
                report->ok = false;
                report->error = "decode failed";
            }
            return { false, "decode failed" };
        }

        // 4c. 缩放（如果配置了缩放）
        AVFrame* scaled_frame = frame;
        if (resolved.scale_w > 0 || resolved.scale_h > 0) {
            int tw = resolved.scale_w > 0 ? resolved.scale_w : frame->width;
            int th = resolved.scale_h > 0 ? resolved.scale_h : frame->height;

            scaled_frame = av_frame_alloc();
            scaled_frame->width  = tw;
            scaled_frame->height = th;
            scaled_frame->format = frame->format;

            // 传递色彩元数据
            scaled_frame->colorspace     = frame->colorspace;
            scaled_frame->color_range     = frame->color_range;
            scaled_frame->color_primaries = frame->color_primaries;
            scaled_frame->color_trc       = frame->color_trc;

            av_frame_get_buffer(scaled_frame, 0);

            int flags = static_cast<int>(resolved.scale_algorithm);
            SwsContext* sws = sws_getContext(
                frame->width, frame->height, (AVPixelFormat)frame->format,
                tw, th, (AVPixelFormat)frame->format,
                flags, nullptr, nullptr, nullptr);

            if (sws) {
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                          scaled_frame->data, scaled_frame->linesize);
                sws_freeContext(sws);
            } else {
                av_frame_free(&scaled_frame);
                scaled_frame = frame; // fallback to original
            }
        }

        // 4d. 编码
        if (resolved.encode.quality < 0 && src.pix_fmt != AV_PIX_FMT_NONE) {
            // 用户未指定 quality：尝试从源等效推算
            // 对于 JPEG 源，可以解析量化表来估算等效质量
            // 此处简化为：使用编码器默认质量（即传入 -1，编码器自行处理）
        }

        ok = media_encode(output, scaled_frame, resolved);

        // 填充报告
        if (report) {
            report->ok = ok;
            report->error = ok ? nullptr : "encode failed";
            report->used_copy = false;
            strncpy(report->src_codec,   src.codec_name, sizeof(report->src_codec) - 1);
            report->src_width    = src.width;
            report->src_height   = src.height;
            strncpy(report->src_pix_fmt, src.pix_fmt_name, sizeof(report->src_pix_fmt) - 1);
            report->src_bit_depth = src.bit_depth;
            report->out_width    = scaled_frame->width;
            report->out_height   = scaled_frame->height;
            // 获取输出编码器名
            const AVCodec* enc = nullptr;
            if (resolved.encode.name)
                enc = avcodec_find_encoder_by_name(resolved.encode.name);
            if (!enc) {
                const char* ext = strrchr(output, '.');
                AVCodecID id = guess_codec_id_for_ext(ext);
                if (id != AV_CODEC_ID_NONE)
                    enc = avcodec_find_encoder(id);
            }
            if (enc) strncpy(report->out_codec, enc->name, sizeof(report->out_codec) - 1);
            if (resolved.encode.pixel_fmt)
                strncpy(report->out_pix_fmt, resolved.encode.pixel_fmt, sizeof(report->out_pix_fmt) - 1);
        }

        // 清理
        if (scaled_frame != frame)
            av_frame_free(&scaled_frame);
        av_frame_free(&frame);
    } else {
        // COPY 成功，填充报告
        if (report) {
            report->ok = true;
            report->error = nullptr;
            report->used_copy = true;
            strncpy(report->src_codec,   src.codec_name, sizeof(report->src_codec) - 1);
            report->src_width    = src.width;
            report->src_height   = src.height;
            strncpy(report->src_pix_fmt, src.pix_fmt_name, sizeof(report->src_pix_fmt) - 1);
            report->src_bit_depth = src.bit_depth;
            strncpy(report->out_codec,   src.codec_name, sizeof(report->out_codec) - 1);
            report->out_width    = src.width;
            report->out_height   = src.height;
            strncpy(report->out_pix_fmt, src.pix_fmt_name, sizeof(report->out_pix_fmt) - 1);
        }
    }

    return ok ? TranscodeResult{ true, nullptr }
              : TranscodeResult{ false, "process failed" };
}

// ============================================================
// 向后兼容
// ============================================================

TranscodeResult convert_image(const char* input, const char* output) {
    ImageConfig cfg;
    cfg.strategy = Strategy::AUTO;  // 自动决策
    ProcessReport report;
    return process_media(input, output, cfg, &report);
}

TranscodeResult transcode_media(const char* input, const char* output) {
    ImageConfig cfg;
    cfg.strategy = Strategy::COPY;  // 强制流拷贝
    return process_media(input, output, cfg);
}
