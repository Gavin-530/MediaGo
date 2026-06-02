// MediaGo - 转码引擎实现

#include "transcoder.h"
#include "media_io.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}

#include <cstring>

// ============================================================
// remux —— 流复制容器转换
// ============================================================

TranscodeResult transcode_media(const char* input, const char* output) {
    if (!input || !output)
        return { false, "null path" };

    // ---- 打开输入 ----
    AVFormatContext* in_fmt = nullptr;
    if (avformat_open_input(&in_fmt, input, nullptr, nullptr) < 0)
        return { false, "cannot open input" };
    if (avformat_find_stream_info(in_fmt, nullptr) < 0) {
        avformat_close_input(&in_fmt);
        return { false, "cannot probe input" };
    }

    // ---- 创建输出 ----
    AVFormatContext* out_fmt = nullptr;
    if (avformat_alloc_output_context2(&out_fmt, nullptr, nullptr, output) < 0) {
        avformat_close_input(&in_fmt);
        return { false, "cannot guess output format" };
    }

    // ---- 流映射 ----
    int stream_count = 0;
    int* map = (int*)av_malloc_array(in_fmt->nb_streams, sizeof(int));
    if (!map) {
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return { false, "alloc failed" };
    }

    for (unsigned i = 0; i < in_fmt->nb_streams; i++) {
        AVStream* in_st = in_fmt->streams[i];

        // 跳过 FFmpeg 不支持的流类型（如 attachment）
        if (in_st->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT)
            continue;

        AVStream* out_st = avformat_new_stream(out_fmt, nullptr);
        if (!out_st)
            continue;
        if (avcodec_parameters_copy(out_st->codecpar, in_st->codecpar) < 0)
            continue;

        out_st->codecpar->codec_tag = 0;          // 让 muxer 自选
        out_st->time_base = in_st->time_base;     // 保持原时基
        map[stream_count++] = (int)i;
    }

    if (stream_count == 0) {
        av_free(map);
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return { false, "no valid streams" };
    }

    // ---- 打开输出文件 ----
    if (!(out_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt->pb, output, AVIO_FLAG_WRITE) < 0) {
            av_free(map);
            avformat_free_context(out_fmt);
            avformat_close_input(&in_fmt);
            return { false, "cannot open output file" };
        }
    }

    // ---- 写头部 ----
    if (avformat_write_header(out_fmt, nullptr) < 0) {
        if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&out_fmt->pb);
        av_free(map);
        avformat_free_context(out_fmt);
        avformat_close_input(&in_fmt);
        return { false, "cannot write header" };
    }

    // ---- 复制数据包 ----
    AVPacket* pkt = av_packet_alloc();
    bool ok = true;

    while (av_read_frame(in_fmt, pkt) >= 0) {
        // 找到对应的输出流索引
        int out_idx = -1;
        for (int j = 0; j < stream_count; j++) {
            if (map[j] == pkt->stream_index) {
                out_idx = j;
                break;
            }
        }
        if (out_idx < 0) {
            av_packet_unref(pkt);
            continue;
        }

        AVStream* in_st  = in_fmt->streams[pkt->stream_index];
        AVStream* out_st = out_fmt->streams[out_idx];

        // 时间戳缩放
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

    // ---- 收尾 ----
    av_write_trailer(out_fmt);

    av_packet_free(&pkt);
    av_free(map);
    if (!(out_fmt->oformat->flags & AVFMT_NOFILE))
        avio_closep(&out_fmt->pb);
    avformat_free_context(out_fmt);
    avformat_close_input(&in_fmt);

    return ok ? TranscodeResult{ true, nullptr }
              : TranscodeResult{ false, "write failed" };
}

// ============================================================
// 图片格式转换
// ============================================================

TranscodeResult convert_image(const char* input, const char* output) {
    if (!input || !output)
        return { false, "null path" };

    int w, h;
    uint8_t* rgba = media_load(input, &w, &h);
    if (!rgba)
        return { false, "cannot decode input image" };

    // 根据扩展名选择编码格式
    const char* ext = strrchr(output, '.');
    bool ok = false;

    if (ext) {
        // 简单大小写不敏感比较（仅比较后缀）
        if (!_stricmp(ext, ".png"))
            ok = media_save_png(output, w, h, rgba);
        else if (!_stricmp(ext, ".jpg") || !_stricmp(ext, ".jpeg"))
            ok = media_save_jpg(output, w, h, rgba, 90);
        else if (!_stricmp(ext, ".bmp") || !_stricmp(ext, ".webp") ||
                 !_stricmp(ext, ".tiff") || !_stricmp(ext, ".tif"))
            // media_save 暂只实现 PNG/JPEG；其他格式通过 transcode_media 转
            ok = false;
    }

    media_free(rgba);

    if (!ok) {
        // 回退：通过 FFmpeg remux 处理（支持所有 FFmpeg 支持的图片格式互转）
        return transcode_media(input, output);
    }

    return { true, nullptr };
}
