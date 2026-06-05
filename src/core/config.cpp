// MediaGo - 配置模块实现
// FFmpeg 编解码器/像素格式枚举

#include "config.h"

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}

// ============================================================
// FFmpeg 环境枚举
// ============================================================

static const char* avtype_str(AVMediaType type) {
    switch (type) {
        case AVMEDIA_TYPE_VIDEO:    return "video";
        case AVMEDIA_TYPE_AUDIO:    return "audio";
        case AVMEDIA_TYPE_SUBTITLE: return "subtitle";
        default:                    return "other";
    }
}

int config_list_codecs(CodecInfo* out, int max_count,
                       bool encoders_only, bool video_only, bool audio_only) {
    if (!out || max_count <= 0) return 0;

    int count = 0;
    const AVCodec* codec = nullptr;
    void* iter = nullptr;

    while ((codec = av_codec_iterate(&iter)) && count < max_count) {
        // 过滤器
        if (encoders_only && !av_codec_is_encoder(codec)) continue;
        if (!encoders_only && !av_codec_is_decoder(codec)) continue;
        if (video_only && codec->type != AVMEDIA_TYPE_VIDEO) continue;
        if (audio_only && codec->type != AVMEDIA_TYPE_AUDIO) continue;

        out[count].name        = codec->name;
        out[count].long_name   = codec->long_name;
        out[count].type        = avtype_str(codec->type);
        out[count].is_encoder  = av_codec_is_encoder(codec);
        out[count].is_decoder  = av_codec_is_decoder(codec);
        out[count].is_hardware = (codec->capabilities & AV_CODEC_CAP_HARDWARE) != 0;
        // 图片编码器：JPEG/WebP/JXL/SVT-JPEGXS
        out[count].is_image    = (strstr(codec->name, "mjpeg") || strstr(codec->name, "jpeg") ||
                                   strstr(codec->name, "webp") || strstr(codec->name, "jxl") ||
                                   strstr(codec->name, "jpegxs"));
        count++;
    }
    return count;
}

int config_list_pixel_fmts(PixelFmtInfo* out, int max_count) {
    if (!out || max_count <= 0) return 0;

    int count = 0;
    const AVPixFmtDescriptor* desc = nullptr;

    // 遍历所有像素格式描述符
    while ((desc = av_pix_fmt_desc_next(desc)) && count < max_count) {
        out[count].name           = desc->name;
        out[count].bits_per_pixel = av_get_bits_per_pixel(desc);
        out[count].log2_chroma_w  = desc->log2_chroma_w;
        out[count].log2_chroma_h  = desc->log2_chroma_h;
        count++;
    }
    return count;
}
