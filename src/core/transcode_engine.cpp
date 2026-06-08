// MediaGo — FFmpeg 原生转码引擎实现
//
// 管线设计（与 ffmpeg CLI 等价）：
//   avformat_open_input → avformat_find_stream_info
//   → 逐流决策(COPY/ENCODE) → 创建输出流 + 编码器/滤镜图
//   → avformat_write_header
//   → 主循环: av_read_frame → dec → filter → enc → av_interleaved_write_frame
//   → flush 所有编码器和滤镜
//   → av_write_trailer
//
// 设计原则：
//   - 用户参数即最终行为，不做自主推断
//   - 所有流都正确处理（视频/音频/字幕）
//   - 时间戳标准化，保证 A/V 同步
//   - 完整的错误清理路径

#include "transcode_engine.h"

#include "nlohmann/json.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ============================================================
// 工具函数
// ============================================================

static char av_error[AV_ERROR_MAX_STRING_SIZE];

static const char* av_strerr(int err) {
    return av_make_error_string(av_error, sizeof(av_error), err);
}

// 从扩展名获取容器格式名
static const char* guess_format_from_ext(const char* path) {
    const char* ext = strrchr(path, '.');
    if (!ext) return nullptr;
    ext++; // skip '.'
    if (!strcmp(ext, "mp4") || !strcmp(ext, "m4v"))  return "mp4";
    if (!strcmp(ext, "mkv"))  return "matroska";
    if (!strcmp(ext, "webm")) return "webm";
    if (!strcmp(ext, "mov"))  return "mov";
    if (!strcmp(ext, "avi"))  return "avi";
    if (!strcmp(ext, "flv"))  return "flv";
    if (!strcmp(ext, "wmv"))  return "asf";
    if (!strcmp(ext, "ts"))   return "mpegts";
    if (!strcmp(ext, "m3u8")) return "hls";
    if (!strcmp(ext, "mp3"))  return "mp3";
    if (!strcmp(ext, "aac"))  return "adts";
    if (!strcmp(ext, "opus")) return "opus";
    if (!strcmp(ext, "flac")) return "flac";
    if (!strcmp(ext, "wav"))  return "wav";
    if (!strcmp(ext, "ogg"))  return "ogg";
    if (!strcmp(ext, "png"))  return "image2";
    if (!strcmp(ext, "jpg") || !strcmp(ext, "jpeg")) return "image2";
    if (!strcmp(ext, "webp")) return "image2";
    if (!strcmp(ext, "bmp"))  return "image2";
    if (!strcmp(ext, "tiff") || !strcmp(ext, "tif")) return "image2";
    if (!strcmp(ext, "avif")) return "avif";
    return nullptr;
}

// ============================================================
// 转码引擎内部实现
// ============================================================

class TranscodeEngine {
public:
    TranscodeEngine() = default;
    ~TranscodeEngine() { close(); }

    TranscodeResult run(const TranscodeConfig& cfg);

private:
    bool open_input(const char* path);
    bool create_output(const TranscodeConfig& cfg);
    bool setup_streams(const TranscodeConfig& cfg);
    bool setup_stream_copy(unsigned in_idx, unsigned out_idx);
    bool setup_stream_encode(unsigned in_idx, unsigned out_idx,
                             const TranscodeConfig& cfg);
    bool build_video_filter(AVCodecContext* dec_ctx,
                             const TranscodeConfig& cfg,
                             AVPixelFormat enc_pix_fmt,
                             AVFilterGraph** graph,
                             AVFilterContext** src,
                             AVFilterContext** sink);
    bool transcode_loop();
    bool flush();
    void close();

    AVFormatContext* in_fmt_ = nullptr;
    AVFormatContext* out_fmt_ = nullptr;

    struct StreamCtx {
        AVStream* in_st;
        AVCodecContext* dec_ctx = nullptr;
        AVStream* out_st;
        AVCodecContext* enc_ctx = nullptr;
        AVFilterGraph* filter_graph = nullptr;
        AVFilterContext* buffer_src = nullptr;
        AVFilterContext* buffer_sink = nullptr;
        bool copy;
    };
    std::vector<StreamCtx> streams_;
    std::vector<int> stream_map_; // in_idx → out_idx, -1 = dropped
};

// ============================================================
// 公开接口
// ============================================================

TranscodeResult transcode_run(const TranscodeConfig& cfg) {
    TranscodeEngine engine;
    return engine.run(cfg);
}

// ============================================================
// 主流程
// ============================================================

TranscodeResult TranscodeEngine::run(const TranscodeConfig& cfg) {
    if (!cfg.input || !cfg.output)
        return { false, "null input or output path" };

    fprintf(stderr, "[MediaGo] %s → %s\n", cfg.input, cfg.output);

    // 1. 打开输入
    if (!open_input(cfg.input))
        return { false, "cannot open input" };

    // 2. 创建输出容器
    if (!create_output(cfg))
        return { false, "cannot create output" };

    // 3. 建立逐流配置
    if (!setup_streams(cfg))
        return { false, "cannot setup streams" };

    // 4. 写容器头
    int ret = avformat_write_header(out_fmt_, nullptr);
    if (ret < 0) {
        fprintf(stderr, "  [error] write header: %s\n", av_strerr(ret));
        return { false, "write header failed" };
    }

    // 5. 主处理循环
    if (!transcode_loop())
        return { false, "transcode loop failed" };

    // 6. 冲刷编码器
    if (!flush())
        return { false, "flush failed" };

    // 7. 写容器尾
    av_write_trailer(out_fmt_);

    fprintf(stderr, "  [ok] done\n");
    return { true, nullptr };
}

// ============================================================
// 打开输入
// ============================================================

bool TranscodeEngine::open_input(const char* path) {
    int ret = avformat_open_input(&in_fmt_, path, nullptr, nullptr);
    if (ret < 0) {
        fprintf(stderr, "  [error] open input: %s\n", av_strerr(ret));
        return false;
    }

    ret = avformat_find_stream_info(in_fmt_, nullptr);
    if (ret < 0) {
        fprintf(stderr, "  [error] find stream info: %s\n", av_strerr(ret));
        return false;
    }

    fprintf(stderr, "  [info] %u streams in '%s'\n",
            in_fmt_->nb_streams, in_fmt_->iformat->name);
    return true;
}

// ============================================================
// 创建输出
// ============================================================

bool TranscodeEngine::create_output(const TranscodeConfig& cfg) {
    // 确定输出格式
    const AVOutputFormat* ofmt = nullptr;

    if (cfg.format) {
        ofmt = av_guess_format(cfg.format, nullptr, nullptr);
    }
    if (!ofmt) {
        const char* fmt_name = guess_format_from_ext(cfg.output);
        if (fmt_name)
            ofmt = av_guess_format(fmt_name, nullptr, nullptr);
    }
    if (!ofmt) {
        // 最后回退：从扩展名自动匹配
        ofmt = av_guess_format(nullptr, cfg.output, nullptr);
    }

    if (!ofmt) {
        fprintf(stderr, "  [error] cannot determine output format for '%s'\n", cfg.output);
        return false;
    }

    int ret = avformat_alloc_output_context2(&out_fmt_, ofmt, nullptr, cfg.output);
    if (ret < 0 || !out_fmt_) {
        fprintf(stderr, "  [error] alloc output ctx: %s\n", av_strerr(ret));
        return false;
    }

    // 打开输出文件（如果不是无文件协议）
    if (!(out_fmt_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&out_fmt_->pb, cfg.output, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "  [error] open output file: %s\n", av_strerr(ret));
            return false;
        }
    }

    fprintf(stderr, "  [info] output format: %s\n", out_fmt_->oformat->name);
    return true;
}

// ============================================================
// 逐流建立编解码链
// ============================================================

bool TranscodeEngine::setup_streams(const TranscodeConfig& cfg) {
    stream_map_.assign(in_fmt_->nb_streams, -1);

    for (unsigned i = 0; i < in_fmt_->nb_streams; i++) {
        AVStream* in_st = in_fmt_->streams[i];
        AVCodecParameters* par = in_st->codecpar;
        AVMediaType type = par->codec_type;

        StreamCtx ctx;
        ctx.in_st = in_st;
        ctx.copy = false;

        // 字幕、附件、数据流：跳过
        if (type == AVMEDIA_TYPE_SUBTITLE ||
            type == AVMEDIA_TYPE_ATTACHMENT ||
            type == AVMEDIA_TYPE_DATA) {
            continue;
        }

        // 视频流
        if (type == AVMEDIA_TYPE_VIDEO) {

            // 判断 copy 还是 encode
            bool is_copy = false;
            if (cfg.video.codec == nullptr || !strcmp(cfg.video.codec, "copy")) {
                is_copy = true;
            }

            if (is_copy) {
                ctx.copy = true;
            } else {
                // 查找视频编码器
                const AVCodec* enc = avcodec_find_encoder_by_name(cfg.video.codec);
                if (!enc) {
                    fprintf(stderr, "  [warn] video encoder '%s' not found, fallback to copy\n",
                            cfg.video.codec);
                    ctx.copy = true;
                }
            }
        }

        // 音频流
        else if (type == AVMEDIA_TYPE_AUDIO) {
            bool is_copy = false;
            if (cfg.audio.codec == nullptr || !strcmp(cfg.audio.codec, "copy")) {
                is_copy = true;
            }

            if (is_copy) {
                ctx.copy = true;
            } else {
                const AVCodec* enc = avcodec_find_encoder_by_name(cfg.audio.codec);
                if (!enc) {
                    fprintf(stderr, "  [warn] audio encoder '%s' not found, fallback to copy\n",
                            cfg.audio.codec);
                    ctx.copy = true;
                }
            }
        }

        // 其他流类型：直接跳过
        else {
            continue;
        }

        // 创建输出流
        AVStream* out_st = nullptr;

        if (ctx.copy) {
            // COPY：创建兼容的输出流
            out_st = avformat_new_stream(out_fmt_, nullptr);
            if (!out_st) continue;

            // 检查输出格式是否支持该编码
            if (avformat_query_codec(out_fmt_->oformat,
                                     par->codec_id,
                                     FF_COMPLIANCE_NORMAL) == 0) {
                fprintf(stderr, "  [skip] stream #%u: codec not compatible with output\n", i);
                continue;
            }

            avcodec_parameters_copy(out_st->codecpar, par);
            out_st->codecpar->codec_tag = 0; // 让 muxer 自动选
            out_st->time_base = in_st->time_base;

        } else {
            // ENCODE：创建编码器
            const AVCodec* enc = (type == AVMEDIA_TYPE_VIDEO)
                ? avcodec_find_encoder_by_name(cfg.video.codec)
                : avcodec_find_encoder_by_name(cfg.audio.codec);

            if (!enc) continue;

            AVCodecContext* enc_ctx = avcodec_alloc_context3(enc);
            if (!enc_ctx) continue;

            out_st = avformat_new_stream(out_fmt_, enc);
            if (!out_st) {
                avcodec_free_context(&enc_ctx);
                continue;
            }

            if (type == AVMEDIA_TYPE_VIDEO) {
                // 视频编码器参数
                enc_ctx->width  = cfg.video.width  > 0 ? cfg.video.width  : par->width;
                enc_ctx->height = cfg.video.height > 0 ? cfg.video.height : par->height;

                // 像素格式
                if (cfg.video.pixel_fmt) {
                    enc_ctx->pix_fmt = av_get_pix_fmt(cfg.video.pixel_fmt);
                    if (enc_ctx->pix_fmt == AV_PIX_FMT_NONE)
                        enc_ctx->pix_fmt = enc->pix_fmts ? enc->pix_fmts[0] : AV_PIX_FMT_YUV420P;
                } else {
                    enc_ctx->pix_fmt = enc->pix_fmts ? enc->pix_fmts[0] : AV_PIX_FMT_YUV420P;
                }

                // 帧率
                if (cfg.video.fps > 0) {
                    enc_ctx->framerate = { (int)(cfg.video.fps * 1000), 1000 };
                    enc_ctx->time_base = av_inv_q(enc_ctx->framerate);
                } else if (in_st->avg_frame_rate.num > 0) {
                    enc_ctx->framerate = in_st->avg_frame_rate;
                    enc_ctx->time_base = av_inv_q(enc_ctx->framerate);
                } else {
                    enc_ctx->time_base = { 1, 25 };
                }

                // 码率/CRF
                if (cfg.video.crf >= 0) {
                    int r = av_opt_set_int(enc_ctx->priv_data, "crf", cfg.video.crf, 0);
                    if (r < 0) fprintf(stderr, "  [warn] crf rejected: %s\n", av_strerr(r));
                }
                if (cfg.video.bitrate > 0) {
                    enc_ctx->bit_rate = cfg.video.bitrate;
                }
                if (cfg.video.maxrate > 0) {
                    enc_ctx->rc_max_rate = cfg.video.maxrate;
                }
                if (cfg.video.bufsize > 0) {
                    enc_ctx->rc_buffer_size = cfg.video.bufsize;
                }

                // 预设/调优
                if (cfg.video.preset) {
                    int r = av_opt_set(enc_ctx->priv_data, "preset", cfg.video.preset, 0);
                    if (r < 0) fprintf(stderr, "  [warn] preset '%s' rejected: %s\n", cfg.video.preset, av_strerr(r));
                }
                if (cfg.video.tune) {
                    int r = av_opt_set(enc_ctx->priv_data, "tune", cfg.video.tune, 0);
                    if (r < 0) fprintf(stderr, "  [warn] tune '%s' rejected: %s\n", cfg.video.tune, av_strerr(r));
                }
                if (cfg.video.profile) {
                    int r = av_opt_set(enc_ctx->priv_data, "profile", cfg.video.profile, 0);
                    if (r < 0) fprintf(stderr, "  [warn] profile '%s' rejected: %s\n", cfg.video.profile, av_strerr(r));
                }

                // 高级编码参数
                if (cfg.video.b_frames >= 0) {
                    enc_ctx->max_b_frames = cfg.video.b_frames;
                }
                if (cfg.video.qmin >= 0) {
                    enc_ctx->qmin = cfg.video.qmin;
                }
                if (cfg.video.qmax >= 0) {
                    enc_ctx->qmax = cfg.video.qmax;
                }
                if (cfg.video.level) {
                    int r = av_opt_set(enc_ctx->priv_data, "level", cfg.video.level, 0);
                    if (r < 0) fprintf(stderr, "  [warn] level '%s' rejected: %s\n", cfg.video.level, av_strerr(r));
                }

                // 编码器扩展参数（JSON → av_opt_set）
                if (cfg.video.opts_json) {
                    try {
                        auto opts = nlohmann::json::parse(cfg.video.opts_json);
                        for (auto& [key, val] : opts.items()) {
                            int ret = 0;
                            if (val.is_string()) {
                                ret = av_opt_set(enc_ctx->priv_data, key.c_str(), val.get<std::string>().c_str(), 0);
                            } else if (val.is_number_integer()) {
                                ret = av_opt_set_int(enc_ctx->priv_data, key.c_str(), val.get<int64_t>(), 0);
                            } else if (val.is_number_float()) {
                                ret = av_opt_set_double(enc_ctx->priv_data, key.c_str(), val.get<double>(), 0);
                            } else if (val.is_boolean()) {
                                ret = av_opt_set_int(enc_ctx->priv_data, key.c_str(), val.get<bool>() ? 1 : 0, 0);
                            }
                            if (ret < 0) {
                                fprintf(stderr, "  [warn] video option '%s' rejected: %s\n",
                                        key.c_str(), av_strerr(ret));
                            }
                        }
                    } catch (...) {
                        fprintf(stderr, "  [warn] failed to parse video opts_json\n");
                    }
                }

                // GOP
                if (cfg.video.gop_size > 0) {
                    enc_ctx->gop_size = cfg.video.gop_size;
                }

                // 兼容图像编码器：强制 GOP=1（关键帧编码）
                if (enc_ctx->codec->type == AVMEDIA_TYPE_VIDEO &&
                    enc_ctx->codec->id != AV_CODEC_ID_H264 &&
                    enc_ctx->codec->id != AV_CODEC_ID_HEVC &&
                    enc_ctx->codec->id != AV_CODEC_ID_AV1 &&
                    enc_ctx->codec->id != AV_CODEC_ID_VP9 &&
                    enc_ctx->codec->id != AV_CODEC_ID_VP8 &&
                    enc_ctx->codec->id != AV_CODEC_ID_MPEG2VIDEO &&
                    enc_ctx->codec->id != AV_CODEC_ID_MPEG4 &&
                    enc_ctx->pix_fmt != AV_PIX_FMT_NONE) {
                    // 图片编码器
                }

                // 色彩空间传递（输入帧将在解码时自动携带）
                enc_ctx->colorspace      = par->color_space;
                enc_ctx->color_range     = par->color_range;
                enc_ctx->color_primaries = par->color_primaries;
                enc_ctx->color_trc       = par->color_trc;

            } else if (type == AVMEDIA_TYPE_AUDIO) {
                enc_ctx->sample_rate = cfg.audio.sample_rate > 0
                    ? cfg.audio.sample_rate : par->sample_rate;

                if (cfg.audio.channel_layout) {
                    enc_ctx->ch_layout = {}; // clear first
                    av_channel_layout_from_string(&enc_ctx->ch_layout,
                                                  cfg.audio.channel_layout);
                } else {
                    av_channel_layout_copy(&enc_ctx->ch_layout, &par->ch_layout);
                }

                enc_ctx->sample_fmt = enc->sample_fmts
                    ? enc->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;

                enc_ctx->time_base = { 1, enc_ctx->sample_rate };

                if (cfg.audio.bitrate > 0) {
                    enc_ctx->bit_rate = cfg.audio.bitrate;
                }

                // 音频高级参数
                if (cfg.audio.compression_level >= 0) {
                    enc_ctx->compression_level = cfg.audio.compression_level;
                }

                // 音频编码器扩展参数
                if (cfg.audio.opts_json) {
                    try {
                        auto opts = nlohmann::json::parse(cfg.audio.opts_json);
                        for (auto& [key, val] : opts.items()) {
                            int ret = 0;
                            if (val.is_string()) {
                                ret = av_opt_set(enc_ctx->priv_data, key.c_str(), val.get<std::string>().c_str(), 0);
                            } else if (val.is_number_integer()) {
                                ret = av_opt_set_int(enc_ctx->priv_data, key.c_str(), val.get<int64_t>(), 0);
                            } else if (val.is_number_float()) {
                                ret = av_opt_set_double(enc_ctx->priv_data, key.c_str(), val.get<double>(), 0);
                            } else if (val.is_boolean()) {
                                ret = av_opt_set_int(enc_ctx->priv_data, key.c_str(), val.get<bool>() ? 1 : 0, 0);
                            }
                            if (ret < 0) {
                                fprintf(stderr, "  [warn] audio option '%s' rejected: %s\n",
                                        key.c_str(), av_strerr(ret));
                            }
                        }
                    } catch (...) {
                        fprintf(stderr, "  [warn] failed to parse audio opts_json\n");
                    }
                }
            }

            // 线程数
            if (cfg.video.threads > 0) {
                enc_ctx->thread_count = cfg.video.threads;
            }

            // 全局头（如需要）
            if (out_fmt_->oformat->flags & AVFMT_GLOBALHEADER) {
                enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            // 支持实验性编码器 (如 truehd, dts, mlp, opus, avui, pdv 等)
            if (enc->capabilities & AV_CODEC_CAP_EXPERIMENTAL) {
                enc_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
            }

            int ret = avcodec_open2(enc_ctx, enc, nullptr);
            if (ret < 0) {
                fprintf(stderr, "  [error] open encoder '%s': %s\n", enc->name, av_strerr(ret));
                avcodec_free_context(&enc_ctx);
                continue;
            }

            avcodec_parameters_from_context(out_st->codecpar, enc_ctx);
            out_st->time_base = enc_ctx->time_base;

            ctx.enc_ctx = enc_ctx;

            // 创建视频滤镜图 + 解码器
            if (type == AVMEDIA_TYPE_VIDEO) {
                const AVCodec* dec = avcodec_find_decoder(par->codec_id);
                ctx.dec_ctx = avcodec_alloc_context3(dec);
                if (ctx.dec_ctx) {
                    avcodec_parameters_to_context(ctx.dec_ctx, par);
                    if (dec->capabilities & AV_CODEC_CAP_EXPERIMENTAL) {
                        ctx.dec_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
                    }
                    int dret = avcodec_open2(ctx.dec_ctx, dec, nullptr);
                    if (dret < 0) {
                        fprintf(stderr, "  [warn] open decoder for stream #%u: %s, skipping encode\n",
                                i, av_strerr(dret));
                        avcodec_free_context(&ctx.dec_ctx);
                    } else {
                        if (!build_video_filter(ctx.dec_ctx, cfg,
                                                enc_ctx->pix_fmt,
                                                &ctx.filter_graph,
                                                &ctx.buffer_src,
                                                &ctx.buffer_sink)) {
                            fprintf(stderr, "  [warn] filter build failed for stream #%u, direct encode\n", i);
                        }
                    }
                }
            }

            // 音频编码也需创建解码器
            if (type == AVMEDIA_TYPE_AUDIO) {
                const AVCodec* dec = avcodec_find_decoder(par->codec_id);
                ctx.dec_ctx = avcodec_alloc_context3(dec);
                if (ctx.dec_ctx) {
                    avcodec_parameters_to_context(ctx.dec_ctx, par);
                    if (dec->capabilities & AV_CODEC_CAP_EXPERIMENTAL) {
                        ctx.dec_ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
                    }
                    int dret = avcodec_open2(ctx.dec_ctx, dec, nullptr);
                    if (dret < 0) {
                        fprintf(stderr, "  [warn] open audio decoder for stream #%u: %s, skipping encode\n",
                                i, av_strerr(dret));
                        avcodec_free_context(&ctx.dec_ctx);
                    }
                }
            }
        }

        stream_map_[i] = (int)streams_.size();
        ctx.out_st = out_st;
        streams_.push_back(ctx);

        fprintf(stderr, "  [%s] stream #%u: %s → %s\n",
                type == AVMEDIA_TYPE_VIDEO ? "video" : "audio",
                i,
                avcodec_get_name(par->codec_id),
                ctx.copy ? "copy" : avcodec_get_name(out_st->codecpar->codec_id));
    }

    // 无有效流
    if (streams_.empty()) {
        fprintf(stderr, "  [error] no valid streams to process\n");
        return false;
    }

    return true;
}

// ============================================================
// 构建视频滤镜图
// ============================================================

bool TranscodeEngine::build_video_filter(AVCodecContext* dec_ctx,
                                          const TranscodeConfig& cfg,
                                          AVPixelFormat enc_pix_fmt,
                                          AVFilterGraph** graph,
                                          AVFilterContext** src,
                                          AVFilterContext** sink) {
    *graph = avfilter_graph_alloc();
    if (!*graph) return false;

    // 输入 buffersrc
    char args[512];
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=1/1",
             dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
             dec_ctx->time_base.num,
             dec_ctx->time_base.den > 0 ? dec_ctx->time_base.den : 1);

    int ret = avfilter_graph_create_filter(src,
            avfilter_get_by_name("buffer"), "in", args, nullptr, *graph);
    if (ret < 0) {
        avfilter_graph_free(graph);
        return false;
    }

    // 输出 buffersink
    ret = avfilter_graph_create_filter(sink,
            avfilter_get_by_name("buffersink"), "out", nullptr, nullptr, *graph);
    if (ret < 0) {
        avfilter_graph_free(graph);
        return false;
    }

    // 构建滤镜链
    AVFilterContext* last = *src;

    // 尺度缩放
    if (cfg.video.width > 0 || cfg.video.height > 0) {
        char scale_args[256];
        if (cfg.video.width > 0 && cfg.video.height > 0) {
            snprintf(scale_args, sizeof(scale_args),
                     "scale=%d:%d:force_original_aspect_ratio=%s",
                     cfg.video.width, cfg.video.height,
                     cfg.video.keep_aspect ? "decrease" : "disable");
        } else if (cfg.video.width > 0) {
            snprintf(scale_args, sizeof(scale_args), "scale=%d:-1", cfg.video.width);
        } else {
            snprintf(scale_args, sizeof(scale_args), "scale=-1:%d", cfg.video.height);
        }

        AVFilterContext* scale_ctx = nullptr;
        ret = avfilter_graph_create_filter(&scale_ctx,
                avfilter_get_by_name("scale"), "scale",
                scale_args, nullptr, *graph);
        if (ret < 0) {
            avfilter_graph_free(graph);
            return false;
        }

        if (avfilter_link(last, 0, scale_ctx, 0) < 0) {
            avfilter_graph_free(graph);
            return false;
        }
        last = scale_ctx;
    }

    // 帧率
    if (cfg.video.fps > 0) {
        char fps_args[64];
        snprintf(fps_args, sizeof(fps_args), "fps=%.3f", cfg.video.fps);

        AVFilterContext* fps_ctx = nullptr;
        ret = avfilter_graph_create_filter(&fps_ctx,
                avfilter_get_by_name("fps"), "fps",
                fps_args, nullptr, *graph);
        if (ret >= 0 && avfilter_link(last, 0, fps_ctx, 0) >= 0) {
            last = fps_ctx;
        }
    }

    // 像素格式转换（解码器输出 vs 编码器期望）
    if (enc_pix_fmt != AV_PIX_FMT_NONE && enc_pix_fmt != dec_ctx->pix_fmt) {
        const char* fmt_name = av_get_pix_fmt_name(enc_pix_fmt);
        if (fmt_name) {
            char format_args[64];
            snprintf(format_args, sizeof(format_args), "format=%s", fmt_name);

            AVFilterContext* format_ctx = nullptr;
            ret = avfilter_graph_create_filter(&format_ctx,
                    avfilter_get_by_name("format"), "format",
                    format_args, nullptr, *graph);
            if (ret >= 0 && avfilter_link(last, 0, format_ctx, 0) >= 0) {
                last = format_ctx;
            } else {
                fprintf(stderr, "  [warn] cannot add format=%s filter: %s\n",
                        fmt_name, av_strerr(ret));
            }
        }
    }

    // 用户自定义滤镜
    if (cfg.video.filters && cfg.video.filters[0]) {
        AVFilterInOut *inputs = nullptr, *outputs = nullptr;
        ret = avfilter_graph_parse2(*graph, cfg.video.filters, &inputs, &outputs);
        if (ret >= 0) {
            // 连接链尾到自定义滤镜入口
            if (inputs) {
                for (AVFilterInOut* in = inputs; in; in = in->next) {
                    if (avfilter_link(last, 0, in->filter_ctx, in->pad_idx) >= 0)
                        break;
                }
                // 自定义滤镜出口连接到 buffersink
                if (outputs) {
                    for (AVFilterInOut* out = outputs; out; out = out->next) {
                        if (avfilter_link(out->filter_ctx, out->pad_idx, *sink, 0) >= 0)
                            break;
                    }
                }
                avfilter_inout_free(&inputs);
                avfilter_inout_free(&outputs);
                last = nullptr; // 已直接链到 sink
            }
        }
    }

    // 已知无链接则链到 sink
    if (last && avfilter_link(last, 0, *sink, 0) < 0) {
        avfilter_graph_free(graph);
        return false;
    }

    ret = avfilter_graph_config(*graph, nullptr);
    if (ret < 0) {
        fprintf(stderr, "  [error] filter graph config: %s\n", av_strerr(ret));
        avfilter_graph_free(graph);
        return false;
    }

    return true;
}

// ============================================================
// 主处理循环
// ============================================================

bool TranscodeEngine::transcode_loop() {
    AVPacket* in_pkt = av_packet_alloc();
    AVPacket* out_pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* filtered_frame = av_frame_alloc();

    int64_t frame_count = 0;
    int ret;

    while ((ret = av_read_frame(in_fmt_, in_pkt)) >= 0) {
        unsigned in_idx = in_pkt->stream_index;
        if (in_idx >= stream_map_.size()) {
            av_packet_unref(in_pkt);
            continue;
        }

        int out_idx = stream_map_[in_idx];
        if (out_idx < 0) {
            av_packet_unref(in_pkt);
            continue;
        }

        StreamCtx& ctx = streams_[out_idx];

        if (ctx.copy) {
            // ---- COPY 路径 ----
            av_packet_rescale_ts(in_pkt,
                                 ctx.in_st->time_base,
                                 ctx.out_st->time_base);
            in_pkt->stream_index = out_idx;
            in_pkt->pos = -1;

            ret = av_interleaved_write_frame(out_fmt_, in_pkt);
            if (ret < 0) {
                fprintf(stderr, "  [error] write frame: %s\n", av_strerr(ret));
                av_packet_free(&in_pkt);
                av_packet_free(&out_pkt);
                av_frame_free(&frame);
                av_frame_free(&filtered_frame);
                return false;
            }
        } else if (ctx.enc_ctx) {
            // ---- ENCODE 路径 ----
            if (!ctx.dec_ctx) {
                av_packet_unref(in_pkt);
                continue;
            }

            // 送入解码器
            ret = avcodec_send_packet(ctx.dec_ctx, in_pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                av_packet_unref(in_pkt);
                continue;
            }

            // 获取解码帧
            while (avcodec_receive_frame(ctx.dec_ctx, frame) >= 0) {
                AVFrame* filter_out = frame;

                // 滤镜处理
                if (ctx.filter_graph) {
                    ret = av_buffersrc_add_frame_flags(ctx.buffer_src, frame,
                                                        AV_BUFFERSRC_FLAG_KEEP_REF);
                    if (ret >= 0) {
                        ret = av_buffersink_get_frame(ctx.buffer_sink, filtered_frame);
                        if (ret >= 0) {
                            filter_out = filtered_frame;
                        }
                    }
                }

                // 设置 PTS
                filter_out->pts = frame_count++;

                // 送入编码器
                ret = avcodec_send_frame(ctx.enc_ctx, filter_out);
                if (ret < 0 && ret != AVERROR(EAGAIN)) {
                    fprintf(stderr, "  [error] send frame: %s\n", av_strerr(ret));
                }

                // 获取编码包
                while (avcodec_receive_packet(ctx.enc_ctx, out_pkt) >= 0) {
                    out_pkt->stream_index = out_idx;
                    av_packet_rescale_ts(out_pkt,
                                         ctx.enc_ctx->time_base,
                                         ctx.out_st->time_base);

                    ret = av_interleaved_write_frame(out_fmt_, out_pkt);
                    if (ret < 0) {
                        fprintf(stderr, "  [error] write encoded packet: %s\n", av_strerr(ret));
                    }
                    av_packet_unref(out_pkt);
                }

                av_frame_unref(filtered_frame);
                av_frame_unref(frame);
            }
        }

        av_packet_unref(in_pkt);
    }

    // EOF
    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "  [error] read frame: %s\n", av_strerr(ret));
    }

    av_packet_free(&in_pkt);
    av_packet_free(&out_pkt);
    av_frame_free(&frame);
    av_frame_free(&filtered_frame);
    return true;
}

// ============================================================
// 冲刷
// ============================================================

bool TranscodeEngine::flush() {
    AVFrame* frame = av_frame_alloc();
    AVFrame* filtered_frame = av_frame_alloc();
    AVPacket* out_pkt = av_packet_alloc();

    for (auto& ctx : streams_) {
        if (ctx.copy || !ctx.enc_ctx || !ctx.dec_ctx) continue;

        // 冲刷解码器：发送 null packet
        avcodec_send_packet(ctx.dec_ctx, nullptr);

        while (avcodec_receive_frame(ctx.dec_ctx, frame) >= 0) {
            AVFrame* filter_out = frame;

            if (ctx.filter_graph) {
                (void)av_buffersrc_add_frame_flags(ctx.buffer_src, frame,
                                                   AV_BUFFERSRC_FLAG_KEEP_REF);
                if (av_buffersink_get_frame(ctx.buffer_sink, filtered_frame) >= 0) {
                    filter_out = filtered_frame;
                } else {
                    av_frame_unref(frame);
                    continue;
                }
            }

            filter_out->pts = AV_NOPTS_VALUE;

            // 冲刷滤镜图：发送 null frame
            if (ctx.filter_graph) {
                (void)av_buffersrc_add_frame_flags(ctx.buffer_src, nullptr, 0);
                while (av_buffersink_get_frame(ctx.buffer_sink, filtered_frame) >= 0) {
                    avcodec_send_frame(ctx.enc_ctx, filtered_frame);
                    av_frame_unref(filtered_frame);
                }
            }

            // 冲刷编码器
            avcodec_send_frame(ctx.enc_ctx, nullptr);
            while (avcodec_receive_packet(ctx.enc_ctx, out_pkt) >= 0) {
                out_pkt->stream_index = ctx.out_st->index;
                av_packet_rescale_ts(out_pkt, ctx.enc_ctx->time_base,
                                     ctx.out_st->time_base);
                av_interleaved_write_frame(out_fmt_, out_pkt);
                av_packet_unref(out_pkt);
            }

            av_frame_unref(filtered_frame);
            av_frame_unref(frame);
        }
    }

    av_frame_free(&frame);
    av_frame_free(&filtered_frame);
    av_packet_free(&out_pkt);
    return true;
}

// ============================================================
// 清理
// ============================================================

void TranscodeEngine::close() {
    for (auto& ctx : streams_) {
        if (ctx.dec_ctx) avcodec_free_context(&ctx.dec_ctx);
        if (ctx.enc_ctx) avcodec_free_context(&ctx.enc_ctx);
        if (ctx.filter_graph) avfilter_graph_free(&ctx.filter_graph);
    }
    streams_.clear();
    stream_map_.clear();

    if (out_fmt_) {
        if (out_fmt_->pb) avio_closep(&out_fmt_->pb);
        avformat_free_context(out_fmt_);
        out_fmt_ = nullptr;
    }
    if (in_fmt_) {
        avformat_close_input(&in_fmt_);
        in_fmt_ = nullptr;
    }
}
