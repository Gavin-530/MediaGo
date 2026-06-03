// MediaGo - 配置模块
// 分层配置架构：用户显式设置 > 格式预设 > 全局默认 > "保持与源一致"
// 核心原则：所选即所得（用户指定则严格执行），不选则保持原始（自动从源采样回填）
//
// 配置来源：
//   1. JSON 配置文件（全局默认 + 格式预设）
//   2. CLI 命令行参数（单次操作覆盖）
//   3. 源文件属性采样（兜底：用户未指定的参数从源文件提取）
//
// 数据结构说明：
//   - 所有可选参数以 int=-1 / const char*=nullptr / enum=NONE/AUTO 表示"未指定"
//   - 未指定参数在流水线执行时从源文件属性回填，保证"不选则保持原始"
//   - 已指定参数直接传入编解码器，不做任何自动修改

#pragma once

#include <cstdint>
#include <cstdio>

// ---- 处理策略 ----

// 策略决定流水线的顶层行为：
//   COPY   - 流拷贝，不解码不重编码，bit-exact。仅当源/目标容器和编码兼容时可用
//   ENCODE - 完整解码→中间帧→重编码。所有参数（编码器、像素格式、质量等）由用户控制
//   AUTO   - 自动决策：同编码+同容器兼容 → COPY；否则 → ENCODE
enum class Strategy {
    COPY = 0,
    ENCODE = 1,
    AUTO = 2,
};

// ---- 缩放 ----

// 缩放模式（仅当 scale_w 或 scale_h > 0 时生效）
enum class ScaleMode {
    NONE = 0,    // 不缩放
    FIT = 1,     // 等比缩放，全部内容可见，可能留黑边（默认）
    FILL = 2,    // 等比缩放，填满目标区域，可能裁剪
    STRETCH = 3, // 拉伸填充，无视宽高比
};

// 缩放算法（对应 sws_scale 的 flags）
// 说明：
enum class ScaleAlgorithm {
    FAST_BILINEAR = 1,  // 双线性，速度快但细节模糊，适合实时预览
    BILINEAR = 2,       // 双线性（精度稍好于 FAST_BILINEAR）
    BICUBIC = 4,        // 双三次，折中选择，质量/性能平衡
    BOX = 8,            // 盒式，适合缩小
    GAUSS = 16,         // 高斯，柔和
    SINC = 32,          // Sinc 函数，理论最优但可能振铃
    LANCZOS = 64,       // Lanczos，图像缩放的行业标准，锐利清晰（推荐）
    SPLINE = 128,       // 样条插值
    AREA = 512,         // 区域平均，适合大幅缩小（如缩略图）
};

// ---- 编码器参数 ----

// 编码参数集合
// 所有字段未指定时，从源文件属性回填
struct CodecParams {
    const char* name = nullptr;   // 编码器名称（如 "libx264", "libx265", "png" 等）
                                  // nullptr = 自动匹配（优先源编码器同名，无则选 FFmpeg 默认）
    const char* pixel_fmt = nullptr; // 像素格式名（如 "yuv420p", "rgb24", "rgba"）
                                     // nullptr = 与源保持一致（优先匹配源的像素格式）
    int quality = -1;                // 质量：PNG=压缩级别(0-9)，JPEG=质量(1-100)，CRF for 视频
                                     // -1 = 自动（从源等效推算）
    int bitrate = 0;                 // 视频比特率 (bps)，0 = 自动
    int gop_size = 0;                // GOP 大小，0 = 编码器默认
    int thread_count = 0;            // 编码线程数，0 = 自动
};

// ---- 图片配置 ----

struct ImageConfig {
    // 处理策略
    Strategy strategy = Strategy::AUTO;

    // 中间格式：从源解码后的像素表示
    // nullptr = 默认与源像素格式一致（如源是 yuvj420p 则中间也用 yuvj420p）
    // 显式设置则强制使用指定格式（如 "rgba" 转为 RGBA8 中间帧）
    // 重要：此处影响解码后的数据精度，选错会丢失位深/色彩空间信息
    const char* intermediate_fmt = nullptr;

    // 缩放（不缩放时 scale_w 和 scale_h 均为 0）
    ScaleMode scale_mode = ScaleMode::NONE;
    int scale_w = 0;
    int scale_h = 0;
    ScaleAlgorithm scale_algorithm = ScaleAlgorithm::LANCZOS;

    // 编码参数（未指定时从源回填）
    CodecParams encode;

    // 元数据保留
    bool preserve_icc = true;        // 保留 ICC 色彩配置文件
    bool preserve_metadata = true;   // 保留 EXIF/XMP/其他元数据
};

// ---- 视频配置 (Phase 3 预留) ----

struct VideoConfig {
    Strategy strategy = Strategy::AUTO;
    CodecParams encode;
    // 更多视频特定参数（帧率、分辨率、预设、profile 等）将在 Phase 3 添加
};

// ---- 音频配置 (Phase 3 预留) ----

struct AudioConfig {
    Strategy strategy = Strategy::AUTO;
    CodecParams encode;
    // 更多音频特定参数（采样率、声道数、码率等）将在 Phase 3 添加
};

// ---- 全局配置 ----

// 全局配置是所有操作的顶层入口
// 优先级（从高到低）：
//   1. CLI 单次操作参数（覆盖此结构）
//   2. JSON 配置文件的显式设置
//   3. 源文件属性采样（兜底）
struct MediaGoConfig {
    ImageConfig image;
    VideoConfig video;   // Phase 3 预留
    AudioConfig audio;   // Phase 3 预留

    // 从 JSON 配置文件加载
    // 返回 true 表示解析成功，字段按"有则覆盖、无则保持默认"合并
    bool from_json(const char* path);

    // 导出为 JSON 配置文件
    // 仅导出非默认值（即用户显式修改过的参数）
    bool to_json(const char* path) const;

    // 打印当前配置（诊断/CLI --help 用）
    void print(FILE* fp = stdout) const;
};

// ---- FFmpeg 环境枚举（供 UI/CLI 展示可用选项）----

// 单条编解码器信息
struct CodecInfo {
    const char* name;       // 编码器名称（如 "libx264"）
    const char* long_name;  // 人类可读名称（如 "libx264 H.264 / AVC / MPEG-4 AVC"）
    const char* type;       // "video" / "audio" / "subtitle"
    bool is_encoder;
    bool is_decoder;
    bool is_hardware;
};

// 像素格式信息
struct PixelFmtInfo {
    const char* name;          // 如 "yuv420p"
    int bits_per_pixel;        // 每像素总位数
    int log2_chroma_w;         // 色度水平子采样（4:2:0 → 1; 4:4:4 → 0）
    int log2_chroma_h;         // 色度垂直子采样
};

// 枚举所有可用的编解码器
// max_count: 最大返回数量；返回实际数量
int config_list_codecs(CodecInfo* out, int max_count,
                       bool encoders_only, bool video_only);

// 枚举所有可用的像素格式
int config_list_pixel_fmts(PixelFmtInfo* out, int max_count);
