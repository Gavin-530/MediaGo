// MediaGo - 主程序入口
// 基于 FFmpeg 的跨平台媒体处理工具
//
// CLI 命令结构：
//   MediaGo convert <input> <output> [options]  — 统一转换（推荐）
//   MediaGo probe   <file>                      — 查看源文件属性
//   MediaGo codecs  [video|audio]               — 列出可用编解码器
//   MediaGo pixfmts                             — 列出可用像素格式
//   MediaGo info                                — FFmpeg 环境诊断
//   MediaGo config [export|load <file>]          — 管理配置
//
//   (向后兼容)
//   MediaGo img    <in> <out>                   — 图片转换
//   MediaGo remux  <in> <out>                   — 容器转封装
//   MediaGo load   <file>                       — 解码图片到 RGBA
//   MediaGo svg    <file> <w> <h>                — SVG 光栅化

#include "core/config.h"
#include "core/media_io.h"
#include "core/transcoder.h"
#include "core/diag.h"

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
    puts("  convert <in> <out> [options]  统一媒体转换（图片/容器）");
    puts("  probe   <file>                查看源文件完整属性");
    puts("  codecs  [video|audio]         列出可用编解码器");
    puts("  pixfmts                       列出可用像素格式");
    puts("  info                          FFmpeg 环境诊断");
    puts("  config  [export|load <file>]  管理配置文件\n");
    puts("convert 选项:");
    puts("  --strategy  copy|encode|auto   处理策略 (默认: auto)");
    puts("  --codec     <name>             指定编码器");
    puts("  --quality   <1-100>            质量参数");
    puts("  --pixfmt    <fmt>              目标像素格式");
    puts("  --scale     <WxH>              缩放尺寸");
    puts("  --scalemode fit|fill|stretch   缩放模式 (默认: fit)");
    puts("  --scalealgo <algo>             缩放算法 (默认: lanczos)");
    puts("  --no-icc                       丢弃 ICC 色彩配置文件");
    puts("  --no-meta                      丢弃元数据\n");
    puts("向后兼容命令:");
    puts("  img    <in> <out>              图片格式转换 (=convert --strategy auto)");
    puts("  remux  <in> <out>              容器转封装 (=convert --strategy copy)");
    puts("  load   <file>                  解码图片到 RGBA 像素");
    puts("  svg    <file> <w> <h>          SVG 光栅化");
}

// ============================================================
// 命令行参数解析辅助
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

    const char* input  = argv[2];
    const char* output = argv[3];

    // 构建 ImageConfig
    ImageConfig cfg;

    printf("=== MediaGo Convert ===\n");
    printf("Input  : %s\n", input);
    printf("Output : %s\n", output);

    // 解析选项
    for (int i = 4; i < argc; i++) {
        const char* arg = argv[i];

        if (!strcmp(arg, "--strategy") && has_arg(argc, argv, i + 1)) {
            const char* v = argv[++i];
            if (!strcmp(v, "copy"))       cfg.strategy = Strategy::COPY;
            else if (!strcmp(v, "encode")) cfg.strategy = Strategy::ENCODE;
            else if (!strcmp(v, "auto"))   cfg.strategy = Strategy::AUTO;
            printf("  strategy = %s\n", v);
        }
        else if (!strcmp(arg, "--codec") && has_arg(argc, argv, i + 1)) {
            cfg.encode.name = argv[++i];
            printf("  codec = %s\n", cfg.encode.name);
        }
        else if (!strcmp(arg, "--quality") && has_arg(argc, argv, i + 1)) {
            cfg.encode.quality = atoi(argv[++i]);
            printf("  quality = %d\n", cfg.encode.quality);
        }
        else if (!strcmp(arg, "--pixfmt") && has_arg(argc, argv, i + 1)) {
            cfg.encode.pixel_fmt = argv[++i];
            printf("  pixel_fmt = %s\n", cfg.encode.pixel_fmt);
        }
        else if (!strcmp(arg, "--scale") && has_arg(argc, argv, i + 1)) {
            const char* v = argv[++i];
            sscanf(v, "%dx%d", &cfg.scale_w, &cfg.scale_h);
            printf("  scale = %dx%d\n", cfg.scale_w, cfg.scale_h);
        }
        else if (!strcmp(arg, "--scalemode") && has_arg(argc, argv, i + 1)) {
            const char* v = argv[++i];
            if (!strcmp(v, "fit"))      cfg.scale_mode = ScaleMode::FIT;
            else if (!strcmp(v, "fill"))     cfg.scale_mode = ScaleMode::FILL;
            else if (!strcmp(v, "stretch"))  cfg.scale_mode = ScaleMode::STRETCH;
            printf("  scale_mode = %s\n", v);
        }
        else if (!strcmp(arg, "--scalealgo") && has_arg(argc, argv, i + 1)) {
            const char* v = argv[++i];
            if (!strcmp(v, "lanczos"))       cfg.scale_algorithm = ScaleAlgorithm::LANCZOS;
            else if (!strcmp(v, "bilinear")) cfg.scale_algorithm = ScaleAlgorithm::BILINEAR;
            else if (!strcmp(v, "bicubic"))  cfg.scale_algorithm = ScaleAlgorithm::BICUBIC;
            else if (!strcmp(v, "nearest"))  cfg.scale_algorithm = ScaleAlgorithm::FAST_BILINEAR;
            else if (!strcmp(v, "area"))     cfg.scale_algorithm = ScaleAlgorithm::AREA;
            printf("  scale_algo = %s\n", v);
        }
        else if (!strcmp(arg, "--no-icc")) {
            cfg.preserve_icc = false;
            printf("  preserve_icc = no\n");
        }
        else if (!strcmp(arg, "--no-meta")) {
            cfg.preserve_metadata = false;
            printf("  preserve_meta = no\n");
        }
        else if (!strcmp(arg, "--intermediate") && has_arg(argc, argv, i + 1)) {
            cfg.intermediate_fmt = argv[++i];
            printf("  intermediate = %s\n", cfg.intermediate_fmt);
        }
        else {
            fprintf(stderr, "未知选项: %s\n", arg);
        }
    }

    printf("\n");

    // 执行
    ProcessReport report;
    memset(&report, 0, sizeof(report));
    TranscodeResult r = process_media(input, output, cfg, &report);

    if (!r.ok) {
        fprintf(stderr, "处理失败: %s\n", r.error ? r.error : "unknown");
        return 1;
    }

    // 打印报告
    printf("=== 处理报告 ===\n");
    printf("  路径      : %s\n", report.used_copy ? "STREAM COPY (bit-exact)" : "DECODE → ENCODE");
    printf("  源编码    : %s\n", report.src_codec);
    printf("  源尺寸    : %dx%d\n", report.src_width, report.src_height);
    printf("  源像素    : %s (%d-bit)\n", report.src_pix_fmt, report.src_bit_depth);
    printf("  输出编码  : %s\n", report.out_codec[0] ? report.out_codec : "(auto)");
    printf("  输出尺寸  : %dx%d\n", report.out_width, report.out_height);
    printf("  输出像素  : %s\n", report.out_pix_fmt[0] ? report.out_pix_fmt : "(auto, same as source)");
    printf("  结果      : OK\n");

    return 0;
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
    printf("  Color Space : %d\n", info.color_space);
    printf("  Color Range : %d\n", info.color_range);
    printf("  Primaries   : %d\n", info.color_primaries);
    printf("  Transfer    : %d\n", info.color_trc);
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
        const char* hw = codecs[i].is_hardware ? " [HW]" : "";
        printf("  %-24s %s%s\n", codecs[i].name, codecs[i].long_name, hw);
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
// config 命令
// ============================================================

static int cmd_config(int argc, char** argv) {
    if (argc < 3) {
        // 无参数：打印当前默认配置
        MediaGoConfig cfg;
        cfg.print();
        return 0;
    }

    if (!strcmp(argv[2], "export") && argc >= 4) {
        MediaGoConfig cfg;
        if (cfg.to_json(argv[3])) {
            printf("配置已导出到: %s\n", argv[3]);
            return 0;
        }
        fprintf(stderr, "导出失败\n");
        return 1;
    }

    if (!strcmp(argv[2], "load") && argc >= 4) {
        MediaGoConfig cfg;
        if (cfg.from_json(argv[3])) {
            printf("配置已加载:\n");
            cfg.print();
            return 0;
        }
        fprintf(stderr, "加载失败\n");
        return 1;
    }

    fprintf(stderr, "用法: MediaGo config [export|load <file>]\n");
    return 1;
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

    // ---- 核心命令 ----
    if (!strcmp(cmd, "convert")) return cmd_convert(argc, argv);
    if (!strcmp(cmd, "probe"))   return cmd_probe(argc, argv);
    if (!strcmp(cmd, "codecs"))  return cmd_codecs(argc, argv);
    if (!strcmp(cmd, "pixfmts")) return cmd_pixfmts(argc, argv);
    if (!strcmp(cmd, "config"))  return cmd_config(argc, argv);
    if (!strcmp(cmd, "info")) {
        diag_run_all();
        return 0;
    }

    // ---- 向后兼容命令 ----
    if (!strcmp(cmd, "img") && argc >= 4) {
        // 转发到 convert
        const char* fake_argv[] = { argv[0], "convert", argv[2], argv[3], nullptr };
        return cmd_convert(4, (char**)fake_argv);
    }

    if (!strcmp(cmd, "remux") && argc >= 4) {
        const char* fake_argv[] = { argv[0], "convert", argv[2], argv[3],
                                    "--strategy", "copy", nullptr };
        return cmd_convert(6, (char**)fake_argv);
    }

    if (!strcmp(cmd, "load") && argc >= 3) {
        AVFrame* frame = nullptr;
        if (!media_decode(argv[2], AV_PIX_FMT_RGBA, &frame)) {
            fprintf(stderr, "解码失败: %s\n", argv[2]);
            return 1;
        }
        printf("OK  %s  (%d x %d)  %s\n", argv[2], frame->width, frame->height,
               av_get_pix_fmt_name((AVPixelFormat)frame->format));
        av_frame_free(&frame);
        return 0;
    }

    if (!strcmp(cmd, "svg") && argc >= 5) {
        int w = atoi(argv[3]);
        int h = atoi(argv[4]);
        int ow = 0, oh = 0;
        uint8_t* rgba = svg_rasterize_ex(argv[2], ScaleMode::FIT, w, h, &ow, &oh);
        if (!rgba) {
            fprintf(stderr, "SVG 解析/光栅化失败: %s\n", argv[2]);
            return 1;
        }
        printf("OK  SVG %s  ->  (%d x %d) RGBA\n", argv[2], ow, oh);
        media_free(rgba);
        return 0;
    }

    print_usage();
    return 1;
}
