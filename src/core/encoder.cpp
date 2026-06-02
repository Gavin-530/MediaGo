#include "encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

int encode_test_video(const char* output_path, int width, int height,
                      int fps, int duration_sec) {
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr,
                                       output_path) < 0)
        return -1;

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) return -1;

    AVStream* stream = avformat_new_stream(fmt_ctx, nullptr);
    AVCodecContext* enc_ctx = avcodec_alloc_context3(codec);
    enc_ctx->width      = width;
    enc_ctx->height     = height;
    enc_ctx->time_base  = {1, fps};
    enc_ctx->framerate  = {fps, 1};
    enc_ctx->pix_fmt    = AV_PIX_FMT_YUV420P;
    enc_ctx->bit_rate   = 400000;
    enc_ctx->gop_size   = 10;
    enc_ctx->max_b_frames = 1;
    av_opt_set(enc_ctx->priv_data, "preset", "fast", 0);
    avcodec_open2(enc_ctx, codec, nullptr);
    avcodec_parameters_from_context(stream->codecpar, enc_ctx);
    stream->time_base = enc_ctx->time_base;

    if (avio_open(&fmt_ctx->pb, output_path, AVIO_FLAG_WRITE) < 0)
        return -1;
    avformat_write_header(fmt_ctx, nullptr);

    AVFrame* frame = av_frame_alloc();
    frame->format = enc_ctx->pix_fmt;
    frame->width  = enc_ctx->width;
    frame->height = enc_ctx->height;
    av_frame_get_buffer(frame, 0);

    AVPacket* pkt = av_packet_alloc();
    int total = fps * duration_sec;

    for (int i = 0; i < total; i++) {
        av_frame_make_writable(frame);
        // 生成测试图案
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++)
                frame->data[0][y * frame->linesize[0] + x] = (x + y + i * 3) % 256;
        for (int y = 0; y < height/2; y++) {
            for (int x = 0; x < width/2; x++) {
                frame->data[1][y * frame->linesize[1] + x] = (x + i * 5) % 256;
                frame->data[2][y * frame->linesize[2] + x] = (y + i * 7) % 256;
            }
        }
        frame->pts = i;
        avcodec_send_frame(enc_ctx, frame);
        while (avcodec_receive_packet(enc_ctx, pkt) == 0) {
            av_packet_rescale_ts(pkt, enc_ctx->time_base, stream->time_base);
            pkt->stream_index = stream->index;
            av_interleaved_write_frame(fmt_ctx, pkt);
            av_packet_unref(pkt);
        }
    }

    avcodec_send_frame(enc_ctx, nullptr);
    while (avcodec_receive_packet(enc_ctx, pkt) == 0) {
        av_packet_rescale_ts(pkt, enc_ctx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        av_interleaved_write_frame(fmt_ctx, pkt);
        av_packet_unref(pkt);
    }

    av_write_trailer(fmt_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avio_closep(&fmt_ctx->pb);
    avcodec_free_context(&enc_ctx);
    avformat_free_context(fmt_ctx);
    return 0;
}
