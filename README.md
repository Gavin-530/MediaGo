# MediaGo

跨平台专业媒体处理工具，基于原生 FFmpeg 管线的音视频、图像处理方案。适用于影视后期、数字资产管理、自动化转码流水线等工业场景。

## 设计理念

| 原则 | 说明 |
|------|------|
| **默认无损** | 不指定编码器即 stream copy，bit-exact 数字级无损 |
| **所选即所得** | 用户显式指定参数才生效，不做自主推断或自动降级 |
| **原生 FFmpeg** | 标准 `avformat → avcodec → avfilter → avcodec → avformat` 五阶段管线 |
| **行业标准输出** | Lanczos 缩放、CRF 质量控制、色彩元数据完整传递 |
| **单一路径** | 整个项目只有一条 TranscodeEngine 处理管线 |

## 架构

```
CLI (src/main.cpp)
 │
 ├── convert ──→ transcode_run() ──→ TranscodeEngine
 │                                         │
 └── batch   ──→ BatchProcessor ───────────┘
                                         │
                    ┌────────────────────┘
                    ▼
              TranscodeEngine 五阶段管线
              ┌──────────────────────────────────────┐
              │ 1. avformat_open_input               │  打开输入
              │ 2. avformat_find_stream_info         │  探测流属性
              │ 3. 逐流决策 (COPY or ENCODE)         │  用户参数决定
              │ 4. transcode_loop                    │  主循环
              │    ├─ av_read_frame                  │  读包
              │    ├─ COPY: av_interleaved_write     │  直写
              │    └─ ENCODE: dec → filter → enc     │  解码/滤镜/编码
              │ 5. flush + av_write_trailer          │  收尾
              └──────────────────────────────────────┘
```

## 项目结构

```
MediaGo/
├── src/
│   ├── main.cpp                      CLI 入口
│   └── core/
│       ├── config.h / .cpp           缩放算法枚举、编解码器/像素格式运行时枚举
│       ├── transcode_config.h        VideoConfig / AudioConfig / TranscodeConfig
│       ├── transcode_engine.h / .cpp FFmpeg 原生转码引擎
│       ├── batch.h / .cpp            JSON 清单批量处理
│       └── media_io.h / .cpp         媒体探测 (probe) + SVG 光栅化
├── devtools/                         开发工具（诊断程序，非发布产品）
│   ├── CMakeLists.txt
│   ├── diag.h / .cpp                 FFmpeg 版本/硬件编解码器/VMAF 检查
│   └── diag_main.cpp                 诊断程序入口
├── libs/
│   ├── ffmpeg/                       FFmpeg 开发包（预编译，Windows MinGW）
│   ├── nanosvg/                      SVG 解析/光栅化（zlib 许可）
│   └── nlohmann/                     JSON 解析（MIT 许可）
├── cmake/
│   └── FindFFmpeg.cmake              自定义 FFmpeg 查找模块
├── CMakeLists.txt
├── .gitignore
└── README.md
```

## 快速开始

### 要求

| 依赖 | 版本 | 说明 |
|------|------|------|
| CMake | >= 3.16 | 构建系统 |
| C++17 编译器 | g++ 8.1+ / Clang 11+ / MSVC 2019+ | |
| FFmpeg 开发包 | 5.0+ | libavcodec / libavformat / libavutil / libswscale / libswresample / libavfilter |

### 构建

```powershell
# Windows MinGW（预编译 FFmpeg 已内置在 libs/ffmpeg/）
cmake -B build -G "MinGW Makefiles"
cmake --build build

# 跳过开发工具（发布构建）
cmake -B build -DBUILD_TESTING=OFF
cmake --build build
```

```bash
# macOS / Linux（需系统安装 FFmpeg 开发包）
cmake -B build
cmake --build build
```

### 构建输出

```
build/
├── MediaGo.exe         主程序
└── devtools/
    └── diag.exe         诊断工具（BUILD_TESTING=ON 时）
```

## 使用

```
MediaGo <command> [args]
```

### convert — 单文件转换

```powershell
MediaGo convert <input> <output> [options]
```

默认不指定 codec = stream copy（bit-exact 无损）。

**选项：**

| 选项 | 参数 | 说明 |
|------|------|------|
| `--vcodec` | `<name>` | 视频编码器，默认 copy |
| `--acodec` | `<name>` | 音频编码器，默认 copy |
| `--crf` | `<0-51>` | CRF 质量控制 |
| `--bitrate` | `<bps>` | 视频码率 |
| `--preset` | `<name>` | 编码器预设 |
| `--scale` | `<WxH>` | 缩放尺寸 |
| `--fps` | `<rate>` | 目标帧率 |
| `--format` | `<name>` | 输出容器格式 |
| `--overwrite` | — | 覆盖已存在文件 |

**示例：**

```powershell
# 纯流拷贝——零质量损失
MediaGo convert input.mp4 output.mp4

# H.264 重编码
MediaGo convert input.mov output.mp4 --vcodec libx264 --crf 23 --preset fast

# 缩放 720p + 换容器
MediaGo convert 4k_source.mp4 hd_output.mkv --vcodec libx264 --crf 18 --scale 1280x720

# 仅重编码音频，视频保持流拷贝
MediaGo convert video.mp4 video_aac.mp4 --acodec aac

# 视频重编码 + 半帧率
MediaGo convert 60fps.mp4 30fps.mp4 --vcodec libx264 --fps 30
```

### batch — 批量处理

```powershell
MediaGo batch <manifest.json>
```

JSON 清单结构：

```json
{
  "output": {
    "dir": "./output",
    "structure": "by_type"
  },
  "defaults": {
    "video": {
      "codec": null,
      "crf": 23,
      "bitrate": 0,
      "width": 0,
      "height": 0,
      "fps": 0.0,
      "keep_aspect": true,
      "preset": "",
      "tune": ""
    },
    "audio": {
      "codec": "",
      "bitrate": 0
    }
  },
  "jobs": [
    {
      "input": "*.mp4",
      "output": null,
      "overwrite": false,
      "format": "",
      "video": {
        "copy": false,
        "codec": "libx264",
        "crf": 18,
        "bitrate": 0,
        "width": 1920,
        "height": 1080,
        "fps": 24,
        "keep_aspect": true,
        "preset": "medium",
        "tune": "film"
      },
      "audio": {
        "copy": false,
        "codec": "aac",
        "bitrate": 256000
      }
    },
    {
      "input": "*.jpg",
      "video": { "copy": true },
      "audio": { "copy": true }
    }
  ]
}
```

**output 字段：**

| 字段 | 值 | 说明 |
|------|-----|------|
| `dir` | `<path>` | 输出根目录 |
| `structure` | `"flat"` / `"by_type"` | `flat`: 所有文件平铺；`by_type`: 按 video/audio/image 分子目录 |

**job 字段：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `input` | string | 文件路径，支持 `*` 和 `?` 通配符 |
| `output` | string | 可选输出路径（相对 `dir` 或绝对路径） |
| `format` | string | 输出容器格式 |
| `overwrite` | bool | 覆盖已存在文件 |
| `video.copy` | bool | `true` = 视频流拷贝 |
| `video.codec` | string | 视频编码器 |
| `video.crf` | int | CRF 质量控制 |
| `video.bitrate` | int | 视频码率 |
| `video.width` / `height` | int | 缩放尺寸 |
| `video.fps` | double | 目标帧率 |
| `video.keep_aspect` | bool | 保持宽高比（默认 true） |
| `video.preset` | string | 编码器预设 |
| `video.tune` | string | 编码器调优 |
| `audio.copy` | bool | `true` = 音频流拷贝 |
| `audio.codec` | string | 音频编码器 |
| `audio.bitrate` | int | 音频码率 |

### probe — 源文件属性探测

```powershell
MediaGo probe <file>

# 输出示例
# === Source Info ===
#   Container   : mov,mp4,m4a,3gp,3g2,mj2
#   Codec       : hevc (id=173)
#   Dimensions  : 3840x2160
#   Pixel Fmt   : yuv420p10le
#   Bit Depth   : 10
#   Alpha       : no
#   ICC Profile : no
#   Streams     : 2
#   Type        : video/audio
```

### codecs — 运行时编解码器枚举

```powershell
MediaGo codecs          # 视频编码器
MediaGo codecs audio    # 音频编码器
```

### pixfmts — 像素格式枚举

```powershell
MediaGo pixfmts
```

## 核心 API

### TranscodeConfig（`src/core/transcode_config.h`）

```cpp
struct VideoConfig {
    const char* codec = nullptr;   // nullptr 或 "copy" = 流拷贝
    int crf = -1;                  // CRF (-1 = 编码器默认)
    int64_t bitrate = 0;           // 码率
    int64_t maxrate = 0;           // 最大码率 (VBV)
    int64_t bufsize = 0;           // 缓冲大小 (VBV)
    const char* preset = nullptr;  // 编码器预设
    const char* tune = nullptr;    // 编码器调优
    int width = 0;                 // 缩放宽度 (0 = 原始)
    int height = 0;                // 缩放高度 (0 = 原始)
    bool keep_aspect = true;       // 保持宽高比
    double fps = 0.0;              // 帧率 (0.0 = 原始)
    const char* pixel_fmt = nullptr;
    int gop_size = 0;
    int threads = 0;
    const char* filters = nullptr; // 自定义 FFmpeg 滤镜图
};

struct AudioConfig {
    const char* codec = nullptr;   // nullptr 或 "copy" = 流拷贝
    int64_t bitrate = 0;
    int sample_rate = 0;
    const char* channel_layout = nullptr;
};

struct TranscodeConfig {
    const char* input;
    const char* output;
    VideoConfig video;
    AudioConfig audio;
    const char* format = nullptr;       // 容器格式 (nullptr = 从扩展名推断)
    bool overwrite = false;
    bool preserve_metadata = true;
};

struct TranscodeResult {
    bool ok = false;
    const char* error = nullptr;
};

TranscodeResult transcode_run(const TranscodeConfig& cfg);
```

### BatchProcessor（`src/core/batch.h`）

```cpp
struct BatchJobItem {
    std::string input;            // 必填，支持通配符
    std::string output;           // 可选
    std::string video_codec;      // 空字符串 = 拷贝
    int video_crf = -1;
    int64_t video_bitrate = 0;
    int video_width = 0;
    int video_height = 0;
    double video_fps = 0.0;
    bool video_keep_aspect = true;
    bool video_copy = false;
    std::string video_preset;
    std::string video_tune;
    std::string audio_codec;      // 空字符串 = 拷贝
    int64_t audio_bitrate = 0;
    bool audio_copy = false;
    std::string format;           // 容器格式
    bool overwrite = false;
};

struct BatchJobResult {
    bool ok = false;
    std::string input;
    std::string output;
    std::string error;
    bool use_copy_video = false;
    bool use_copy_audio = false;
    int64_t elapsed_ms = 0;
};

enum class JobStatus { Processing, OK, Fail };

using ProgressCallback = std::function<void(unsigned index, unsigned total,
                                             JobStatus status,
                                             const std::string& input)>;

class BatchProcessor {
public:
    bool process(const char* manifest_path,
                 ProgressCallback on_progress = nullptr);
};
```

### media_probe（`src/core/media_io.h`）

```cpp
struct SourceInfo {
    char codec_name[64];       // "hevc" / "mjpeg" / "png" 等
    int codec_id;              // AVCodecID 枚举值
    int width, height;         // 像素尺寸
    AVPixelFormat pix_fmt;     // AV_PIX_FMT_*
    char pix_fmt_name[32];     // "yuv420p10le" 等
    int bit_depth;             // 8 / 10 / 12 / 16
    int color_space;           // AVColorSpace
    int color_range;           // AVColorRange
    int color_primaries;       // AVColorPrimaries
    int color_trc;             // 传递函数
    char container[32];        // 容器格式名
    bool has_icc;              // 是否嵌入 ICC Profile
    bool has_alpha;            // 是否含透明通道
    int nb_streams;            // 总流数
    bool is_image;             // true = 单帧图片
};

bool media_probe(const char* path, SourceInfo* info);
```

### 运行时枚举（`src/core/config.h`）

```cpp
struct CodecInfo {
    const char* name;       // "libx264"
    const char* long_name;  // "libx264 H.264 / AVC / MPEG-4 AVC"
    const char* type;       // "video" / "audio" / "subtitle"
    bool is_encoder;
    bool is_decoder;
    bool is_hardware;
};

struct PixelFmtInfo {
    const char* name;          // "yuv420p"
    int bits_per_pixel;
    int log2_chroma_w;         // 色度水平子采样
    int log2_chroma_h;         // 色度垂直子采样
};

int config_list_codecs(CodecInfo* out, int max_count,
                       bool encoders_only, bool video_only);
int config_list_pixel_fmts(PixelFmtInfo* out, int max_count);
```

### 缩放枚举（`src/core/config.h`）

```cpp
enum class ScaleMode {
    NONE = 0,    // 不缩放
    FIT = 1,     // 等比缩放，全部内容可见，可能留黑边
    FILL = 2,    // 等比缩放，填满目标区域，可能裁剪
    STRETCH = 3, // 拉伸填充，无视宽高比
};

enum class ScaleAlgorithm {
    FAST_BILINEAR = 1,
    BILINEAR = 2,
    BICUBIC = 4,
    BOX = 8,
    GAUSS = 16,
    SINC = 32,
    LANCZOS = 64,    // 行业标准，锐利清晰（默认）
    SPLINE = 128,
    AREA = 512,      // 适合大幅缩小（缩略图）
};
```

## 支持的格式

### 容器格式映射

| 文件扩展名 | FFmpeg 容器名 |
|------------|--------------|
| `.mp4` `.m4v` | `mp4` |
| `.mkv` | `matroska` |
| `.webm` | `webm` |
| `.mov` | `mov` |
| `.avi` | `avi` |
| `.flv` | `flv` |
| `.wmv` | `asf` |
| `.ts` | `mpegts` |
| `.m3u8` | `hls` |
| `.mp3` | `mp3` |
| `.aac` | `adts` |
| `.opus` | `opus` |
| `.flac` | `flac` |
| `.wav` | `wav` |
| `.ogg` | `ogg` |
| `.png` `.jpg` `.jpeg` `.webp` `.bmp` `.tiff` `.tif` | `image2` |
| `.avif` | `avif` |

### 图片格式

PNG / JPEG / BMP / WebP / AVIF / TIFF / HEIF / HEIC / ICO / GIF / DNG / SVG

DNG 为 Bayer raw 格式，不支持去马赛克（仅 probe）。SVG 矢量图通过 nanosvg 光栅化后编码。

### 视频格式

MP4 / MKV / AVI / MOV / WebM / FLV / WMV / M4V / MPEG / TS / M2TS / 3GP / OGV / MXF

### 音频格式

MP3 / WAV / FLAC / AAC / OGG / Opus / WMA

## FFmpeg 模块使用

| 库 | 使用模块 | 用途 |
|----|---------|------|
| `libavcodec` | transcode_engine / config / media_io | 编解码、编解码器枚举 |
| `libavformat` | transcode_engine / media_io | 容器封装/解封装、流探测 |
| `libavutil` | transcode_engine / media_io | 像素格式、帧管理、内存分配 |
| `libswscale` | transcode_engine | 像素格式转换、缩放 |
| `libswresample` | transcode_engine | 音频采样率/声道布局转换 |
| `libavfilter` | transcode_engine | 视频滤镜图（scale / fps / 自定义） |

## CMake 参考

### 自定义 FindFFmpeg 模块

`cmake/FindFFmpeg.cmake` 按以下优先级查找 FFmpeg：

1. **pkg-config** — macOS/Linux 系统安装
2. **FFMPEG_ROOT** — 环境变量指定路径
3. **项目内置** — `libs/ffmpeg/`（Windows MinGW 预编译包）

创建 IMPORTED 目标：`FFmpeg::avcodec` / `FFmpeg::avformat` / `FFmpeg::avutil` / `FFmpeg::swscale` / `FFmpeg::swresample` / `FFmpeg::avfilter`。

## 许可

| 组件 | 许可 |
|------|------|
| MediaGo 自身 | GPL |
| FFmpeg | LGPL / GPL（取决于编译选项） |
| nanosvg | zlib |
| nlohmann/json | MIT |
