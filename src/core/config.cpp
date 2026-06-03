// MediaGo - 配置模块实现
// JSON 读写 + FFmpeg 编解码器/像素格式枚举
//
// 分层合并逻辑（from_json）：
//   1. 读取 JSON 文件
//   2. 遍历顶层 (image/video/audio)，对各子节点逐字段解析
//   3. "有则覆盖当前值，无则保持默认（即 nullptr/-1）"
//   4. 同级子节点互不影响，部分覆盖
//
// 导出逻辑（to_json）：
//   仅序列化非默认值，避免输出冗余（如 -1/0/nullptr/默认枚举值 不写进 JSON）

#include "config.h"

#include "../../libs/nlohmann/json.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}

#include <cstring>
#include <string>

using json = nlohmann::json;

// ============================================================
// 内部辅助：枚举 ↔ 字符串
// ============================================================

static Strategy parse_strategy(const std::string& s) {
    if (s == "copy")   return Strategy::COPY;
    if (s == "encode") return Strategy::ENCODE;
    if (s == "auto")   return Strategy::AUTO;
    return Strategy::AUTO;
}

static const char* strategy_str(Strategy s) {
    switch (s) {
        case Strategy::COPY:   return "copy";
        case Strategy::ENCODE: return "encode";
        default:               return "auto";
    }
}

static ScaleMode parse_scale_mode(const std::string& s) {
    if (s == "fit")     return ScaleMode::FIT;
    if (s == "fill")    return ScaleMode::FILL;
    if (s == "stretch") return ScaleMode::STRETCH;
    return ScaleMode::NONE;
}

static const char* scale_mode_str(ScaleMode m) {
    switch (m) {
        case ScaleMode::FIT:     return "fit";
        case ScaleMode::FILL:    return "fill";
        case ScaleMode::STRETCH: return "stretch";
        default:                 return "none";
    }
}

static ScaleAlgorithm parse_scale_algo(const std::string& s) {
    if (s == "fast_bilinear") return ScaleAlgorithm::FAST_BILINEAR;
    if (s == "bilinear")      return ScaleAlgorithm::BILINEAR;
    if (s == "bicubic")       return ScaleAlgorithm::BICUBIC;
    if (s == "box")           return ScaleAlgorithm::BOX;
    if (s == "gauss")         return ScaleAlgorithm::GAUSS;
    if (s == "sinc")          return ScaleAlgorithm::SINC;
    if (s == "lanczos")       return ScaleAlgorithm::LANCZOS;
    if (s == "spline")        return ScaleAlgorithm::SPLINE;
    if (s == "area")          return ScaleAlgorithm::AREA;
    return ScaleAlgorithm::LANCZOS;
}

static const char* scale_algo_str(ScaleAlgorithm a) {
    switch (a) {
        case ScaleAlgorithm::FAST_BILINEAR: return "fast_bilinear";
        case ScaleAlgorithm::BILINEAR:      return "bilinear";
        case ScaleAlgorithm::BICUBIC:       return "bicubic";
        case ScaleAlgorithm::BOX:           return "box";
        case ScaleAlgorithm::GAUSS:         return "gauss";
        case ScaleAlgorithm::SINC:          return "sinc";
        case ScaleAlgorithm::LANCZOS:       return "lanczos";
        case ScaleAlgorithm::SPLINE:        return "spline";
        case ScaleAlgorithm::AREA:          return "area";
        default:                            return "lanczos";
    }
}

// ---- 从 JSON 读取 CodecParams ----

static void parse_codec_params(CodecParams& cp, const json& j) {
    if (j.is_null()) return;
    if (j.contains("codec") && j["codec"].is_string())
        cp.name = _strdup(j["codec"].get<std::string>().c_str());
    if (j.contains("pixel_fmt") && j["pixel_fmt"].is_string())
        cp.pixel_fmt = _strdup(j["pixel_fmt"].get<std::string>().c_str());
    if (j.contains("quality") && j["quality"].is_number_integer())
        cp.quality = j["quality"].get<int>();
    if (j.contains("bitrate") && j["bitrate"].is_number_integer())
        cp.bitrate = j["bitrate"].get<int>();
    if (j.contains("gop_size") && j["gop_size"].is_number_integer())
        cp.gop_size = j["gop_size"].get<int>();
    if (j.contains("threads") && j["threads"].is_number_integer())
        cp.thread_count = j["threads"].get<int>();
}

// ---- 导出 CodecParams 到 JSON（仅非默认值）----

static json dump_codec_params(const CodecParams& cp) {
    json j;
    if (cp.name)           j["codec"]     = cp.name;
    if (cp.pixel_fmt)      j["pixel_fmt"] = cp.pixel_fmt;
    if (cp.quality >= 0)   j["quality"]   = cp.quality;
    if (cp.bitrate > 0)    j["bitrate"]   = cp.bitrate;
    if (cp.gop_size > 0)   j["gop_size"]  = cp.gop_size;
    if (cp.thread_count > 0) j["threads"] = cp.thread_count;
    return j;
}

// ---- 从 JSON 读取 ImageConfig ----

static void parse_image_config(ImageConfig& ic, const json& j) {
    if (j.is_null()) return;

    if (j.contains("strategy") && j["strategy"].is_string())
        ic.strategy = parse_strategy(j["strategy"].get<std::string>());

    if (j.contains("intermediate") && j["intermediate"].is_string())
        ic.intermediate_fmt = _strdup(j["intermediate"].get<std::string>().c_str());

    // scale 子对象
    if (j.contains("scale") && j["scale"].is_object()) {
        const auto& s = j["scale"];
        if (s.contains("mode") && s["mode"].is_string())
            ic.scale_mode = parse_scale_mode(s["mode"].get<std::string>());
        if (s.contains("width") && s["width"].is_number_integer())
            ic.scale_w = s["width"].get<int>();
        if (s.contains("height") && s["height"].is_number_integer())
            ic.scale_h = s["height"].get<int>();
        if (s.contains("algorithm") && s["algorithm"].is_string())
            ic.scale_algorithm = parse_scale_algo(s["algorithm"].get<std::string>());
    }

    // encode 子对象
    if (j.contains("encode"))
        parse_codec_params(ic.encode, j["encode"]);

    if (j.contains("preserve_icc") && j["icc"].is_boolean())
        ic.preserve_icc = j["icc"].get<bool>();
    if (j.contains("preserve_metadata") && j["preserve_metadata"].is_boolean())
        ic.preserve_metadata = j["preserve_metadata"].get<bool>();
}

// ---- 导出 ImageConfig 到 JSON（仅非默认值）----

static json dump_image_config(const ImageConfig& ic) {
    json j;
    j["strategy"] = strategy_str(ic.strategy);

    if (ic.intermediate_fmt)
        j["intermediate"] = ic.intermediate_fmt;

    // scale
    if (ic.scale_w > 0 || ic.scale_h > 0 || ic.scale_mode != ScaleMode::NONE) {
        json s;
        s["mode"]      = scale_mode_str(ic.scale_mode);
        s["width"]     = ic.scale_w;
        s["height"]    = ic.scale_h;
        s["algorithm"] = scale_algo_str(ic.scale_algorithm);
        j["scale"]     = s;
    }

    // encode
    json enc = dump_codec_params(ic.encode);
    if (!enc.empty())
        j["encode"] = enc;

    j["preserve_icc"]      = ic.preserve_icc;
    j["preserve_metadata"] = ic.preserve_metadata;

    return j;
}

// ---- 打印 CodecParams ----

static void print_codec_params(const CodecParams& cp, FILE* fp, const char* indent) {
    fprintf(fp, "%s  codec     : %s\n", indent, cp.name ? cp.name : "(auto, from source)");
    fprintf(fp, "%s  pixel_fmt : %s\n", indent, cp.pixel_fmt ? cp.pixel_fmt : "(auto, same as source)");
    fprintf(fp, "%s  quality   : %d\n", indent, cp.quality);
    fprintf(fp, "%s  bitrate   : %d\n", indent, cp.bitrate);
    fprintf(fp, "%s  gop_size  : %d\n", indent, cp.gop_size);
    fprintf(fp, "%s  threads   : %d\n", indent, cp.thread_count);
}

// ============================================================
// MediaGoConfig 成员函数
// ============================================================

bool MediaGoConfig::from_json(const char* path) {
    if (!path) return false;

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string content(size, '\0');
    fread(&content[0], 1, size, f);
    fclose(f);

    try {
        json root = json::parse(content);

        if (root.contains("image"))
            parse_image_config(image, root["image"]);
        // video / audio 预留，Phase 3 补充
        // if (root.contains("video")) parse_video_config(video, root["video"]);
        // if (root.contains("audio")) parse_audio_config(audio, root["audio"]);

        return true;
    } catch (const json::exception&) {
        return false;
    }
}

bool MediaGoConfig::to_json(const char* path) const {
    if (!path) return false;

    try {
        json root;
        root["image"] = dump_image_config(image);
        // video / audio 预留
        // root["video"] = dump_video_config(video);
        // root["audio"] = dump_audio_config(audio);

        std::string s = root.dump(4);
        FILE* f = fopen(path, "wb");
        if (!f) return false;
        fwrite(s.c_str(), 1, s.size(), f);
        fclose(f);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

void MediaGoConfig::print(FILE* fp) const {
    fprintf(fp, "=== MediaGo Configuration ===\n\n");

    fprintf(fp, "[Image]\n");
    fprintf(fp, "  strategy      : %s\n", strategy_str(image.strategy));
    fprintf(fp, "  intermediate  : %s\n",
            image.intermediate_fmt ? image.intermediate_fmt : "(same as source)");
    fprintf(fp, "  scale_mode    : %s\n", scale_mode_str(image.scale_mode));
    fprintf(fp, "  scale_w       : %d\n", image.scale_w);
    fprintf(fp, "  scale_h       : %d\n", image.scale_h);
    fprintf(fp, "  scale_algo    : %s\n", scale_algo_str(image.scale_algorithm));
    fprintf(fp, "  preserve_icc  : %s\n", image.preserve_icc ? "yes" : "no");
    fprintf(fp, "  preserve_meta : %s\n", image.preserve_metadata ? "yes" : "no");
    fprintf(fp, "  [encode]\n");
    print_codec_params(image.encode, fp, "  ");

    fprintf(fp, "\n[Video] (Phase 3 reserved)\n");
    fprintf(fp, "  strategy : %s\n", strategy_str(video.strategy));

    fprintf(fp, "\n[Audio] (Phase 3 reserved)\n");
    fprintf(fp, "  strategy : %s\n", strategy_str(audio.strategy));

    fprintf(fp, "\n=============================\n");
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
                       bool encoders_only, bool video_only) {
    if (!out || max_count <= 0) return 0;

    int count = 0;
    const AVCodec* codec = nullptr;
    void* iter = nullptr;

    while ((codec = av_codec_iterate(&iter)) && count < max_count) {
        // 过滤器
        if (encoders_only && !av_codec_is_encoder(codec)) continue;
        if (!encoders_only && !av_codec_is_decoder(codec)) continue;
        if (video_only && codec->type != AVMEDIA_TYPE_VIDEO) continue;

        out[count].name        = codec->name;
        out[count].long_name   = codec->long_name;
        out[count].type        = avtype_str(codec->type);
        out[count].is_encoder  = av_codec_is_encoder(codec);
        out[count].is_decoder  = av_codec_is_decoder(codec);
        out[count].is_hardware = (codec->capabilities & AV_CODEC_CAP_HARDWARE) != 0;
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
