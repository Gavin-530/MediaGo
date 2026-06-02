// MediaGo - 主程序入口
// 基于 FFmpeg 的跨平台媒体处理工具

#include "core/media_io.h"
#include "core/transcoder.h"
#include "core/diag.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void print_usage() {
    puts("MediaGo - 跨平台媒体处理工具\n");
    puts("用法:");
    puts("  MediaGo info              FFmpeg 环境诊断");
    puts("  MediaGo load <file>       解码图片到 RGBA 像素（测试 media_io）");
    puts("  MediaGo svg <file> <w> <h> SVG 光栅化到 RGBA（测试 nanosvg）");
    puts("  MediaGo img <in> <out>    图片格式转换（PNG/JPEG/BMP/WebP 等）");
    puts("  MediaGo remux <in> <out>  容器转换/转封装（MP4/MKV/WebM 等）");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char* cmd = argv[1];

    if (!strcmp(cmd, "info")) {
        diag_run_all();
        return 0;
    }

    if (!strcmp(cmd, "load") && argc >= 3) {
        int w, h;
        uint8_t* rgba = media_load(argv[2], &w, &h);
        if (!rgba) {
            fprintf(stderr, "解码失败: %s\n", argv[2]);
            return 1;
        }
        printf("OK  %s  (%d x %d) RGBA\n", argv[2], w, h);
        media_free(rgba);
        return 0;
    }

    if (!strcmp(cmd, "svg") && argc >= 5) {
        int w = atoi(argv[3]);
        int h = atoi(argv[4]);
        uint8_t* rgba = svg_rasterize(argv[2], w, h);
        if (!rgba) {
            fprintf(stderr, "SVG 解析/光栅化失败: %s\n", argv[2]);
            return 1;
        }
        printf("OK  SVG %s  ->  (%d x %d) RGBA\n", argv[2], w, h);
        media_free(rgba);
        return 0;
    }

    if (!strcmp(cmd, "img") && argc >= 4) {
        TranscodeResult r = convert_image(argv[2], argv[3]);
        if (!r.ok) {
            fprintf(stderr, "图片转换失败: %s\n", r.error ? r.error : "unknown");
            return 1;
        }
        printf("OK  %s  ->  %s\n", argv[2], argv[3]);
        return 0;
    }

    if (!strcmp(cmd, "remux") && argc >= 4) {
        TranscodeResult r = transcode_media(argv[2], argv[3]);
        if (!r.ok) {
            fprintf(stderr, "转封装失败: %s\n", r.error ? r.error : "unknown");
            return 1;
        }
        printf("OK  %s  ->  %s\n", argv[2], argv[3]);
        return 0;
    }

    print_usage();
    return 1;
}
