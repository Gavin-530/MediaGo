// MediaGo - 统一媒体 I/O 实现
// 源文件属性探测 + SVG 矢量图光栅化

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION

#include "media_io.h"
#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/mem.h>
}

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
