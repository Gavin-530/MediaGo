// MediaGo - 主程序入口
// 基于 FFmpeg 的跨平台媒体处理工具
//
// CLI 命令结构：
//   MediaGo convert <input> <output> [options]  — 统一转换
//   MediaGo batch   <manifest.json>             — 批量处理
//   MediaGo probe   <file>                      — 查看源文件属性
//   MediaGo codecs  [video|audio]               — 列出可用编解码器
//   MediaGo pixfmts                             — 列出可用像素格式

#include "core/config.h"
#include "core/media_io.h"
#include "core/transcode_engine.h"
#include "core/batch.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void print_usage() {
    puts("MediaGo - 专业媒体处理工具\n");
    puts("核心命令:");
    puts("  convert <in> <out> [options]  统一媒体转换");
    puts("  batch   <manifest.json>       批量处理（视频/音频/图像混合）");
    puts("  probe   <file>                查看源文件完整属性");
    puts("  codecs  [video|audio]         列出可用编解码器");
    puts("  pixfmts                       列出可用像素格式\n");
    puts("convert 选项:");
    puts("  --vcodec    <name>             视频编码器 (默认: copy, 不重编码)");
    puts("  --acodec    <name>             音频编码器 (默认: copy)");
    puts("  --crf       <0-51>             CRF 质量 (x264/x265 等)");
    puts("  --bitrate   <bps>              目标码率");
    puts("  --preset    <preset>           编码器预设 (medium/fast/slow...)");
    puts("  --scale     <WxH>              缩放尺寸");
    puts("  --fps       <fps>              目标帧率");
    puts("  --format    <fmt>              容器格式 (mp4/mkv/avi...)");
    puts("  --overwrite                    覆盖已存在的输出文件\n");
    puts("\nbatch 清单格式示例:");
    puts("  { \"output\": {\"dir\":\"./output\"}, \"defaults\": {");
    puts("      \"video\": {\"codec\":\"libx264\",\"crf\":23},");
    puts("      \"audio\": {\"codec\":\"aac\",\"bitrate\":128000}");
    puts("    }, \"jobs\": [");
    puts("      { \"input\":\"videos/*.mp4\" },");
    puts("      { \"input\":\"photo.jpg\", \"video\":{\"copy\":true} }");
    puts("  ]}");
}

// ============================================================
// 参数解析
// ============================================================

static bool has_arg(int argc, char** argv, int i) {
    return i < argc && argv[i] && argv[i][0] != '-';
}

// ============================================================
// convert 命令
// ============================================================

static int cmd_convert(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "用法: MediaGo convert <input> <output> [options]\n");
        return 1;
    }

    TranscodeConfig cfg;
    cfg.input  = argv[2];
    cfg.output = argv[3];

    for (int i = 4; i < argc; i++) {
        const char* arg = argv[i];

        if (!strcmp(arg, "--vcodec") && has_arg(argc, argv, i + 1)) {
            cfg.video.codec = argv[++i];
        }
        else if (!strcmp(arg, "--acodec") && has_arg(argc, argv, i + 1)) {
            cfg.audio.codec = argv[++i];
        }
        else if (!strcmp(arg, "--crf") && has_arg(argc, argv, i + 1)) {
            cfg.video.crf = atoi(argv[++i]);
        }
        else if (!strcmp(arg, "--bitrate") && has_arg(argc, argv, i + 1)) {
            cfg.video.bitrate = atoll(argv[++i]);
        }
        else if (!strcmp(arg, "--preset") && has_arg(argc, argv, i + 1)) {
            cfg.video.preset = argv[++i];
        }
        else if (!strcmp(arg, "--scale") && has_arg(argc, argv, i + 1)) {
            sscanf(argv[++i], "%dx%d", &cfg.video.width, &cfg.video.height);
        }
        else if (!strcmp(arg, "--fps") && has_arg(argc, argv, i + 1)) {
            cfg.video.fps = atof(argv[++i]);
        }
        else if (!strcmp(arg, "--format") && has_arg(argc, argv, i + 1)) {
            cfg.format = argv[++i];
        }
        else if (!strcmp(arg, "--overwrite")) {
            cfg.overwrite = true;
        }
        else {
            fprintf(stderr, "未知选项: %s\n", arg);
        }
    }

    TranscodeResult r = transcode_run(cfg);
    return r.ok ? 0 : 1;
}

// ============================================================
// probe 命令
// ============================================================

static int cmd_probe(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "用法: MediaGo probe <file>\n");
        return 1;
    }

    SourceInfo info;
    if (!media_probe(argv[2], &info)) {
        fprintf(stderr, "探测失败: %s\n", argv[2]);
        return 1;
    }

    printf("=== Source Info ===\n");
    printf("  Container   : %s\n", info.container);
    printf("  Codec       : %s (id=%d)\n", info.codec_name, info.codec_id);
    printf("  Dimensions  : %dx%d\n", info.width, info.height);
    printf("  Pixel Fmt   : %s\n", info.pix_fmt_name);
    printf("  Bit Depth   : %d\n", info.bit_depth);
    printf("  Alpha       : %s\n", info.has_alpha ? "yes" : "no");
    printf("  ICC Profile : %s\n", info.has_icc ? "yes" : "no");
    printf("  Streams     : %d\n", info.nb_streams);
    printf("  Type        : %s\n", info.is_image ? "image" : "video/audio");
    return 0;
}

// ============================================================
// codecs 命令
// ============================================================

static int cmd_codecs(int argc, char** argv) {
    bool video_only = true;
    if (argc >= 3 && !strcmp(argv[2], "audio"))
        video_only = false;

    printf("=== Available %s Encoders ===\n", video_only ? "Video" : "Audio");

    CodecInfo codecs[200];
    int n = config_list_codecs(codecs, 200, true, video_only);

    for (int i = 0; i < n; i++) {
        printf("  %-24s %s%s\n", codecs[i].name, codecs[i].long_name,
               codecs[i].is_hardware ? " [HW]" : "");
    }
    printf("  Total: %d\n", n);
    return 0;
}

// ============================================================
// pixfmts 命令
// ============================================================

static int cmd_pixfmts(int argc, char** argv) {
    (void)argc; (void)argv;

    printf("=== Available Pixel Formats ===\n");

    PixelFmtInfo fmts[200];
    int n = config_list_pixel_fmts(fmts, 200);

    for (int i = 0; i < n; i++) {
        const char* chroma;
        if (fmts[i].log2_chroma_w == 0 && fmts[i].log2_chroma_h == 0)
            chroma = "4:4:4";
        else if (fmts[i].log2_chroma_w == 1 && fmts[i].log2_chroma_h == 0)
            chroma = "4:2:2";
        else if (fmts[i].log2_chroma_w == 1 && fmts[i].log2_chroma_h == 1)
            chroma = "4:2:0";
        else
            chroma = "?";
        printf("  %-24s %3d bpp  %s\n", fmts[i].name, fmts[i].bits_per_pixel, chroma);
    }
    printf("  Total: %d\n", n);
    return 0;
}

// ============================================================
// batch 命令
// ============================================================

static int cmd_batch(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "用法: MediaGo batch <manifest.json>\n");
        return 1;
    }

    BatchProcessor bp;
    return bp.process(argv[2]) ? 0 : 1;
}

// ============================================================
// 主入口
// ============================================================

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char* cmd = argv[1];

    if (!strcmp(cmd, "convert")) return cmd_convert(argc, argv);
    if (!strcmp(cmd, "probe"))   return cmd_probe(argc, argv);
    if (!strcmp(cmd, "batch"))   return cmd_batch(argc, argv);
    if (!strcmp(cmd, "codecs"))  return cmd_codecs(argc, argv);
    if (!strcmp(cmd, "pixfmts")) return cmd_pixfmts(argc, argv);

    // 无命令参数时直接当文件名处理
    print_usage();
    return 1;
}
