// MediaGo - 统一媒体 I/O 实现
// 光栅图：FFmpeg avcodec 编解码（覆盖 PNG/JPEG/BMP/WebP/AVIF/HEIF 等）
// 矢量图：nanosvg 解析 + 光栅化
// 内存统一管理：av_malloc / av_free
//
// 重构要点（v2）：
//   - media_probe：不解码像素，仅提取源文件属性
//   - media_decode：支持配置目标像素格式，AV_PIX_FMT_NONE=保留源格式
//   - media_encode：统一编码接口，编码器/质量/格式全由 Config 控制
//   - media_stream_copy：同格式 bit-exact 拷贝
//   - svg_rasterize_ex：接受 ScaleConfig，支持 fit/fill/stretch 模式

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION

#include "media_io.h"
#include "../../libs/nanosvg/nanosvg.h"
#include "../../libs/nanosvg/nanosvgrast.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <cstdio>
#include <cstring>

// ============================================================
// 内部辅助
// ============================================================

// 从像素格式描述符获取位深
static int get_bit_depth(AVPixelFormat fmt) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
    if (!desc) return 8;
    int max_bits = 0;
    for (int i = 0; i < desc->nb_components; i++) {
        if (desc->comp[i].depth > max_bits)
            max_bits = desc->comp[i].depth;
    }
    return max_bits > 0 ? max_bits : 8;
}

// 根据输出扩展名推断 AVCodecID（编码器自动选择用）
static AVCodecID guess_codec_id(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return AV_CODEC_ID_NONE;

    if (!_stricmp(ext, ".png"))  return AV_CODEC_ID_PNG;
    if (!_stricmp(ext, ".jpg") || !_stricmp(ext, ".jpeg")) return AV_CODEC_ID_MJPEG;
    if (!_stricmp(ext, ".webp")) return AV_CODEC_ID_WEBP;
    if (!_stricmp(ext, ".bmp"))  return AV_CODEC_ID_BMP;
    if (!_stricmp(ext, ".tiff") || !_stricmp(ext, ".tif")) return AV_CODEC_ID_TIFF;
    if (!_stricmp(ext, ".avif")) return AV_CODEC_ID_AV1;   // AVIF = AV1 still image

    return AV_CODEC_ID_NONE;
}

// ---- ScaleAlgorithm → sws_scale flags ----
// (值直接映射，无需转换函数)

// ============================================================
// media_probe —— 探测源文件属性（不解码像素）
// ============================================================

bool media_probe(const char* path, SourceInfo* info) {
    if (!path || !info) return false;
    memset(info, 0, sizeof(SourceInfo));
    info->pix_fmt = AV_PIX_FMT_NONE;

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, path, nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // 容器信息
    if (fmt_ctx->iformat && fmt_ctx->iformat->name) {
        strncpy(info->container, fmt_ctx->iformat->name, sizeof(info->container) - 1);
    }

    info->nb_streams = fmt_ctx->nb_streams;
    info->is_image = (fmt_ctx->nb_streams == 1);

    // 查找第一个视频流
    const AVCodec* dec = nullptr;
    int vidx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (vidx >= 0 && dec) {
        AVCodecParameters* par = fmt_ctx->streams[vidx]->codecpar;

        // 编解码信息
        strncpy(info->codec_name, dec->name, sizeof(info->codec_name) - 1);
        info->codec_id = par->codec_id;
        info->width    = par->width;
        info->height   = par->height;

        // 像素格式
        info->pix_fmt = (AVPixelFormat)par->format;
        const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(info->pix_fmt);
        if (desc) {
            strncpy(info->pix_fmt_name, desc->name, sizeof(info->pix_fmt_name) - 1);
            info->has_alpha = (desc->flags & AV_PIX_FMT_FLAG_ALPHA) != 0;
        }
        info->bit_depth = par->bits_per_coded_sample > 0
                          ? par->bits_per_coded_sample : get_bit_depth(info->pix_fmt);

        // 色彩空间
        info->color_space     = par->color_space;
        info->color_range     = par->color_range;
        info->color_primaries = par->color_primaries;
        info->color_trc       = par->color_trc;
    }

    // ICC Profile 检测（简单通过 metadata 字典检查）
    AVDictionaryEntry* tag = av_dict_get(fmt_ctx->streams[vidx]->metadata,
                                         "icc_profile", nullptr, 0);
    info->has_icc = (tag != nullptr);

    avformat_close_input(&fmt_ctx);
    return true;
}

// ============================================================
// media_decode —— 解码到指定像素格式
// ============================================================

bool media_decode(const char* path, AVPixelFormat dst_fmt,
                  AVFrame** frame_out) {
    if (!path || !frame_out) return false;
    *frame_out = nullptr;

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, path, nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    const AVCodec* dec = nullptr;
    int stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (stream_idx < 0 || !dec) {
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx) {
        avformat_close_input(&fmt_ctx);
        return false;
    }
    if (avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[stream_idx]->codecpar) < 0 ||
        avcodec_open2(dec_ctx, dec, nullptr) < 0) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* raw_frame = av_frame_alloc();
    AVFrame* result = nullptr;

    // 解码第一个视频帧
    while (!result && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != stream_idx) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(dec_ctx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        if (avcodec_receive_frame(dec_ctx, raw_frame) >= 0) {
            AVPixelFormat src_fmt = (AVPixelFormat)raw_frame->format;

            // 如果目标格式为 NONE，直接使用解码器原始输出（不用 sws）
            if (dst_fmt == AV_PIX_FMT_NONE || dst_fmt == src_fmt) {
                result = av_frame_clone(raw_frame);
            } else {
                // 需要像素格式转换
                result = av_frame_alloc();
                result->width  = raw_frame->width;
                result->height = raw_frame->height;
                result->format = dst_fmt;

                // 保留色彩空间信息到输出帧
                result->colorspace     = raw_frame->colorspace;
                result->color_range     = raw_frame->color_range;
                result->color_primaries = raw_frame->color_primaries;
                result->color_trc       = raw_frame->color_trc;

                if (av_frame_get_buffer(result, 0) < 0) {
                    av_frame_free(&result);
                    break;
                }

                SwsContext* sws = sws_getContext(
                    raw_frame->width, raw_frame->height, src_fmt,
                    result->width, result->height, dst_fmt,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);

                if (!sws) {
                    av_frame_free(&result);
                    break;
                }

                sws_scale(sws, raw_frame->data, raw_frame->linesize,
                          0, raw_frame->height, result->data, result->linesize);
                sws_freeContext(sws);
            }
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&raw_frame);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);

    *frame_out = result;
    return result != nullptr;
}

// ============================================================
// media_encode —— 统一编码接口
// ============================================================

bool media_encode(const char* path, const AVFrame* frame,
                  const ImageConfig& cfg) {
    if (!path || !frame) return false;

    // 1. 决定编码器
    const AVCodec* enc = nullptr;
    if (cfg.encode.name) {
        // 用户指定编码器名
        enc = avcodec_find_encoder_by_name(cfg.encode.name);
    }
    if (!enc) {
        // 自动从扩展名推断
        AVCodecID id = guess_codec_id(path);
        if (id != AV_CODEC_ID_NONE)
            enc = avcodec_find_encoder(id);
    }
    if (!enc) return false;

    // 2. 创建编码器上下文
    AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
    if (!enc_ctx) return false;

    enc_ctx->width  = frame->width;
    enc_ctx->height = frame->height;
    enc_ctx->time_base = { 1, 1 };

    // 3. 决定目标像素格式
    AVPixelFormat dst_fmt;
    if (cfg.encode.pixel_fmt) {
        // 用户指定像素格式
        dst_fmt = av_get_pix_fmt(cfg.encode.pixel_fmt);
        if (dst_fmt == AV_PIX_FMT_NONE) {
            // 找不到则 fallback 到编码器首选
            dst_fmt = enc->pix_fmts ? enc->pix_fmts[0] : (AVPixelFormat)frame->format;
        }
    } else {
        // 未指定：优先使用帧的当前格式（如果编码器支持），否则取编码器首选
        dst_fmt = (AVPixelFormat)frame->format;
        if (enc->pix_fmts) {
            bool supported = false;
            for (int i = 0; enc->pix_fmts[i] != AV_PIX_FMT_NONE; i++) {
                if (enc->pix_fmts[i] == dst_fmt) {
                    supported = true;
                    break;
                }
            }
            if (!supported)
                dst_fmt = enc->pix_fmts[0];
        }
    }
    enc_ctx->pix_fmt = dst_fmt;

    // 4. 质量/码率控制
    // MJPEG: quality → qscale 映射（quality 1-100 → qscale 31-1）
    if (enc->id == AV_CODEC_ID_MJPEG && cfg.encode.quality >= 0) {
        int q = 31 - (cfg.encode.quality * 30 / 100);
        if (q < 1)  q = 1;
        if (q > 31) q = 31;
        enc_ctx->global_quality = q * FF_QP2LAMBDA;
        enc_ctx->flags |= AV_CODEC_FLAG_QSCALE;
    }
    // PNG: compression_level（0-9）
    if (enc->id == AV_CODEC_ID_PNG && cfg.encode.quality >= 0) {
        int level = cfg.encode.quality;
        if (level < 0) level = 0;
        if (level > 9) level = 9;
        av_opt_set_int(enc_ctx->priv_data, "compression_level", level, 0);
    }
    // 通用 quality 尝试
    if (cfg.encode.quality >= 0 && enc->id != AV_CODEC_ID_MJPEG && enc->id != AV_CODEC_ID_PNG) {
        // 对大多数编码器，尝试用 global_quality 设置（CRF 编码器会忽略）
        enc_ctx->global_quality = cfg.encode.quality * FF_QP2LAMBDA;
    }

    // 线程数
    if (cfg.encode.thread_count > 0)
        enc_ctx->thread_count = cfg.encode.thread_count;

    // GOP
    if (cfg.encode.gop_size > 0)
        enc_ctx->gop_size = cfg.encode.gop_size;

    // 5. 打开编码器
    if (avcodec_open2(enc_ctx, enc, nullptr) < 0) {
        avcodec_free_context(&enc_ctx);
        return false;
    }

    // 6. 如果需要像素格式转换，构造中间帧
    const AVFrame* enc_frame = frame;
    AVFrame* converted = nullptr;

    if ((AVPixelFormat)frame->format != enc_ctx->pix_fmt) {
        converted = av_frame_alloc();
        converted->width  = frame->width;
        converted->height = frame->height;
        converted->format = enc_ctx->pix_fmt;

        // 传递色彩元数据
        converted->colorspace     = frame->colorspace;
        converted->color_range     = frame->color_range;
        converted->color_primaries = frame->color_primaries;
        converted->color_trc       = frame->color_trc;

        if (av_frame_get_buffer(converted, 0) < 0) {
            av_frame_free(&converted);
            avcodec_free_context(&enc_ctx);
            return false;
        }

        SwsContext* sws = sws_getContext(
            frame->width, frame->height, (AVPixelFormat)frame->format,
            frame->width, frame->height, enc_ctx->pix_fmt,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!sws) {
            av_frame_free(&converted);
            avcodec_free_context(&enc_ctx);
            return false;
        }

        sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                  converted->data, converted->linesize);
        sws_freeContext(sws);
        enc_frame = converted;
    }

    // 7. 编码并写入文件
    FILE* f = fopen(path, "wb");
    if (!f) {
        av_frame_free(&converted);
        avcodec_free_context(&enc_ctx);
        return false;
    }

    bool ok = true;
    if (avcodec_send_frame(enc_ctx, enc_frame) < 0) ok = false;
    if (avcodec_send_frame(enc_ctx, nullptr) < 0) ok = false;

    AVPacket* pkt = av_packet_alloc();
    while (ok && avcodec_receive_packet(enc_ctx, pkt) >= 0) {
        fwrite(pkt->data, 1, pkt->size, f);
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    fclose(f);
    av_frame_free(&converted);
    avcodec_free_context(&enc_ctx);
    return ok;
}

// ============================================================
// media_stream_copy —— bit-exact 流拷贝
// ============================================================

bool media_stream_copy(const char* input, const char* output) {
    if (!input || !output) return false;

    AVFormatContext* in_fmt = nullptr;
    if (avformat_open_input(&in_fmt, input, nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(in_fmt, nullptr) < 0) {
        avformat_close_input(&in_fmt);
        return false;
    }

    AVFormatContext* out_fmt = nullptr;
    if (avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output) < 0) {
        avformat_close_input(&in_fmt);
        return false;
    }

    // 流映射
    int* map = (int*)av_malloc_array(in_fmt->nb_streams, sizeof(int));
    if (!map) {
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return false;
    }

    int stream_count = 0;
    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        AVStream* in_st = in_fmt->streams[i];
        if (in_st->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT)
            continue;

        AVStream* out_st = avformat_new_stream(out_fmt, nullptr);
        if (!out_st) continue;
        if (avcodec_parameters_copy(out_st->codecpar, in_st->codecpar) < 0)
            continue;

        out_st->codecpar->codec_tag = 0;
        out_st->time_base = in_st->time_base;
        map[stream_count++] = (int)i;
    }

    if (stream_count == 0) {
        av_free(map);
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return false;
    }

    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, output, AVIO_FLAG_WRITE) < 0) {
            av_free(map);
            avformat_free_context(out_fmt);
            avformat_close_input(&in_fmt);
            return false;
        }
    }

    if (avformat_write_header(out_fmt, nullptr) < 0) {
        if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&out_fmt->pb);
        av_free(map);
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return false;
    }

    AVPacket* pkt = av_packet_alloc();
    bool ok = true;

    while (av_read_frame(in_fmt, pkt) >= 0) {
        int out_idx = -1;
        for (int j = 0; j < stream_count; j++) {
            if (map[j] == pkt->stream_index) { out_idx = j; break; }
        }
        if (out_idx < 0) { av_packet_unref(pkt); continue; }

        AVStream* in_st  = in_fmt->streams[pkt->stream_index];
        AVStream* out_st = out_fmt->streams[out_idx];

        av_packet_rescale_ts(pkt, in_st->time_base, out_st->time_base);
        pkt->stream_index = out_idx;
        pkt->pos = -1;

        if (av_interleaved_write_frame(out_fmt, pkt) < 0) {
            av_packet_unref(pkt);
            ok = false;
            break;
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(out_fmt);

    av_packet_free(&pkt);
    av_free(map);
    if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);

    return ok;
}

// ============================================================
// SVG 光栅化（支持 ScaleConfig）
// ============================================================

uint8_t* svg_rasterize_ex(const char* path, const ScaleMode mode,
                           int scale_w, int scale_h, int* w_out, int* h_out) {
    if (!path || !w_out || !h_out) return nullptr;

    NSVGimage* svg = nsvgParseFromFile(path, "px", 96);
    if (!svg) return nullptr;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(svg);
        return nullptr;
    }

    // 计算目标尺寸
    int tw = scale_w > 0 ? scale_w : (int)svg->width;
    int th = scale_h > 0 ? scale_h : (int)svg->height;
    if (tw <= 0) tw = (int)svg->width;
    if (th <= 0) th = (int)svg->height;

    double svg_w = svg->width;
    double svg_h = svg->height;
    double scale = 1.0;
    double offset_x = 0, offset_y = 0;

    switch (mode) {
        case ScaleMode::FIT: {
            // 等比缩放，全部内容可见
            scale = (double)tw / svg_w;
            if (svg_h * scale > th)
                scale = (double)th / svg_h;
            // 居中
            offset_x = ((double)tw - svg_w * scale) / 2.0;
            offset_y = ((double)th - svg_h * scale) / 2.0;
            break;
        }
        case ScaleMode::FILL: {
            // 等比缩放，填满，可能裁剪
            scale = (double)tw / svg_w;
            if (svg_h * scale < th)
                scale = (double)th / svg_h;
            // 居中
            offset_x = ((double)tw - svg_w * scale) / 2.0;
            offset_y = ((double)th - svg_h * scale) / 2.0;
            break;
        }
        case ScaleMode::STRETCH: {
            // 拉伸，不等比
            // nanosvg 只支持等比缩放，以 X 为基准（非精确拉伸，后续可用多通道实现）
            double sx = (double)tw / svg_w;
            (void)sx;
            scale = (double)tw / svg_w;
            offset_x = 0;
            offset_y = ((double)th - svg_h * scale) / 2.0;
            break;
        }
        default: // NONE: 使用 SVG 原始尺寸
            tw = (int)svg_w;
            th = (int)svg_h;
            scale = 1.0;
            break;
    }

    uint8_t* img = (uint8_t*)av_malloc((size_t)tw * th * 4);
    if (!img) {
        nsvgDeleteRasterizer(rast);
        nsvgDelete(svg);
        return nullptr;
    }

    nsvgRasterize(rast, svg, offset_x, offset_y, scale, img, tw, th, tw * 4);

    *w_out = tw;
    *h_out = th;

    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);
    return img;
}

// 向后兼容包装
uint8_t* svg_rasterize(const char* path, int w, int h) {
    int ow, oh;
    return svg_rasterize_ex(path, ScaleMode::FIT, w, h, &ow, &oh);
}

// ============================================================
// 内存释放
// ============================================================

void media_free(uint8_t* data) {
    av_free(data);
}
