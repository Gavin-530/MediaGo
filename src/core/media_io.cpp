// MediaGo - 统一媒体 I/O 实现
// 光栅图：FFmpeg avcodec 编解码（覆盖 PNG/JPEG/BMP/WebP/AVIF/HEIF 等）
// 矢量图：nanosvg 解析 + 光栅化
// 内存统一管理：av_malloc / av_free

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
#include <libswscale/swscale.h>
}

#include <cstdio>
#include <cstring>

// ============================================================
// 光栅图 —— FFmpeg codec 统一处理
// ============================================================

uint8_t* media_load(const char* path, int* w, int* h) {
    if (!path || !w || !h) return nullptr;

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, path, nullptr, nullptr) < 0)
        return nullptr;
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return nullptr;
    }

    const AVCodec* dec = nullptr;
    int stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (stream_idx < 0 || !dec) {
        avformat_close_input(&fmt_ctx);
        return nullptr;
    }

    AVCodecContext* dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx) {
        avformat_close_input(&fmt_ctx);
        return nullptr;
    }
    if (avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[stream_idx]->codecpar) < 0) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return nullptr;
    }
    if (avcodec_open2(dec_ctx, dec, nullptr) < 0) {
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return nullptr;
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    uint8_t* result = nullptr;

    // 读取并解码第一个视频帧
    while (result == nullptr && av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index != stream_idx) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(dec_ctx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);

        if (avcodec_receive_frame(dec_ctx, frame) >= 0) {
            int out_w = frame->width;
            int out_h = frame->height;
            if (out_w <= 0 || out_h <= 0) break;

            SwsContext* sws = sws_getContext(
                frame->width, frame->height, (AVPixelFormat)frame->format,
                out_w, out_h, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);

            if (sws) {
                result = (uint8_t*)av_malloc((size_t)out_w * out_h * 4);
                if (result) {
                    uint8_t* dst[4] = { result, nullptr, nullptr, nullptr };
                    int dst_linesize[4] = { out_w * 4, 0, 0, 0 };
                    sws_scale(sws, frame->data, frame->linesize,
                              0, out_h, dst, dst_linesize);
                    *w = out_w;
                    *h = out_h;
                }
                sws_freeContext(sws);
            }
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    return result;
}

// ---- 编码辅助 ----

static bool encode_rgba_to_file(const char* path, int w, int h,
                                 const uint8_t* rgba, AVCodecID codec_id,
                                 int quality) {
    const AVCodec* enc = avcodec_find_encoder(codec_id);
    if (!enc) return false;

    AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
    if (!enc_ctx) return false;

    enc_ctx->width  = w;
    enc_ctx->height = h;
    enc_ctx->time_base = { 1, 1 };

    // 选择编码器首选的像素格式
    AVPixelFormat dst_fmt = enc->pix_fmts ? enc->pix_fmts[0] : AV_PIX_FMT_YUVJ420P;
    enc_ctx->pix_fmt = dst_fmt;

    // JPEG 质量控制
    if (codec_id == AV_CODEC_ID_MJPEG && quality > 0) {
        // MJPEG: qscale 2-31, 越小质量越高; 映射 quality(1-100) 到 qscale(31-1)
        int q = 31 - (quality * 30 / 100);
        if (q < 1)  q = 1;
        if (q > 31) q = 31;
        enc_ctx->global_quality = q * FF_QP2LAMBDA;
        enc_ctx->flags |= AV_CODEC_FLAG_QSCALE;
    }

    if (avcodec_open2(enc_ctx, enc, nullptr) < 0) {
        avcodec_free_context(&enc_ctx);
        return false;
    }

    // RGBA → 编码器像素格式
    AVFrame* frame = av_frame_alloc();
    frame->width  = w;
    frame->height = h;
    frame->format = dst_fmt;
    if (av_frame_get_buffer(frame, 0) < 0) {
        av_frame_free(&frame);
        avcodec_free_context(&enc_ctx);
        return false;
    }

    SwsContext* sws = sws_getContext(w, h, AV_PIX_FMT_RGBA,
                                      w, h, dst_fmt,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        av_frame_free(&frame);
        avcodec_free_context(&enc_ctx);
        return false;
    }

    const uint8_t* src[4] = { rgba, nullptr, nullptr, nullptr };
    int src_linesize[4] = { w * 4, 0, 0, 0 };
    sws_scale(sws, src, src_linesize, 0, h, frame->data, frame->linesize);
    sws_freeContext(sws);

    // 编码
    FILE* f = fopen(path, "wb");
    if (!f) {
        av_frame_free(&frame);
        avcodec_free_context(&enc_ctx);
        return false;
    }

    bool ok = true;
    if (avcodec_send_frame(enc_ctx, frame) < 0) ok = false;
    if (avcodec_send_frame(enc_ctx, nullptr) < 0) ok = false;  // flush

    AVPacket* pkt = av_packet_alloc();
    while (ok && avcodec_receive_packet(enc_ctx, pkt) >= 0) {
        fwrite(pkt->data, 1, pkt->size, f);
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    fclose(f);
    av_frame_free(&frame);
    avcodec_free_context(&enc_ctx);
    return ok;
}

bool media_save_png(const char* path, int w, int h, const uint8_t* data) {
    return encode_rgba_to_file(path, w, h, data, AV_CODEC_ID_PNG, 0);
}

bool media_save_jpg(const char* path, int w, int h, const uint8_t* data, int quality) {
    return encode_rgba_to_file(path, w, h, data, AV_CODEC_ID_MJPEG, quality);
}

void media_free(uint8_t* data) {
    av_free(data);
}

// ============================================================
// 矢量图 —— nanosvg 解析 + 光栅化
// ============================================================

uint8_t* svg_rasterize(const char* path, int w, int h) {
    if (!path || w <= 0 || h <= 0) return nullptr;

    NSVGimage* svg = nsvgParseFromFile(path, "px", 96);
    if (!svg) return nullptr;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(svg);
        return nullptr;
    }

    // 统一使用 av_malloc，与 media_load 返回的内存同源
    uint8_t* img = (uint8_t*)av_malloc((size_t)w * h * 4);
    if (!img) {
        nsvgDeleteRasterizer(rast);
        nsvgDelete(svg);
        return nullptr;
    }

    double scale = (double)w / svg->width;
    if (svg->height * scale > h)
        scale = (double)h / svg->height;

    nsvgRasterize(rast, svg, 0, 0, scale, img, w, h, w * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);
    return img;
}
