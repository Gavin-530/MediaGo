#include "diag.h"
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavfilter/avfilter.h>
}

void diag_print_version() {
    std::cout << "[FFmpeg Version]" << std::endl;
    std::cout << "  " << av_version_info() << std::endl;
    std::cout << "  libavcodec  : " << AV_STRINGIFY(LIBAVCODEC_VERSION) << std::endl;
    std::cout << "  libavformat : " << AV_STRINGIFY(LIBAVFORMAT_VERSION) << std::endl;
    std::cout << "  libavutil   : " << AV_STRINGIFY(LIBAVUTIL_VERSION) << std::endl;
    std::cout << "  libavfilter : " << AV_STRINGIFY(LIBAVFILTER_VERSION) << std::endl;
    std::cout << std::endl;
}

void diag_print_config() {
    std::cout << "[Build Configuration]" << std::endl;
    std::cout << "  " << avcodec_configuration() << std::endl;
    std::cout << std::endl;
}

static const char* codec_type_str(AVMediaType type) {
    switch (type) {
        case AVMEDIA_TYPE_VIDEO:    return "Video";
        case AVMEDIA_TYPE_AUDIO:    return "Audio";
        case AVMEDIA_TYPE_SUBTITLE: return "Subtitle";
        default:                    return "Other";
    }
}

void diag_list_hw_decoders() {
    std::cout << "[Hardware Decoders]" << std::endl;
    int count = 0;
    const AVCodec* codec = nullptr;
    void* iter = nullptr;
    while ((codec = av_codec_iterate(&iter))) {
        if (av_codec_is_decoder(codec) &&
            (codec->capabilities & AV_CODEC_CAP_HARDWARE)) {
            std::cout << "  [HW] " << codec->name
                      << " (" << codec_type_str(codec->type) << ")"
                      << " - " << (codec->long_name ? codec->long_name : "") << std::endl;
            count++;
        }
    }
    std::cout << "  Total: " << count << std::endl;
    std::cout << std::endl;
}

void diag_list_hw_encoders() {
    std::cout << "[Hardware Encoders]" << std::endl;
    int count = 0;
    const AVCodec* codec = nullptr;
    void* iter = nullptr;
    while ((codec = av_codec_iterate(&iter))) {
        if (av_codec_is_encoder(codec) &&
            (codec->capabilities & AV_CODEC_CAP_HARDWARE)) {
            std::cout << "  [HW] " << codec->name
                      << " (" << codec_type_str(codec->type) << ")"
                      << " - " << (codec->long_name ? codec->long_name : "") << std::endl;
            count++;
        }
    }
    std::cout << "  Total: " << count << std::endl;
    std::cout << std::endl;
}

void diag_check_vmaf() {
    std::cout << "[VMAF]" << std::endl;
    const AVFilter* vmaf = avfilter_get_by_name("libvmaf");
    if (vmaf) {
        std::cout << "  libvmaf: AVAILABLE" << std::endl;
    } else {
        std::cout << "  libvmaf: NOT FOUND" << std::endl;
    }
    std::cout << std::endl;
}

void diag_run_all() {
    std::cout << "======== MediaGo Diagnosis ========" << std::endl;
    std::cout << std::endl;
    diag_print_version();
    diag_list_hw_decoders();
    diag_list_hw_encoders();
    diag_check_vmaf();
    std::cout << "===================================" << std::endl;
}
