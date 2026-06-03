# MediaGo

跨平台专业媒体处理工具，基于 FFmpeg + nanosvg 的轻量级音视频与图像处理方案。

## 设计理念

MediaGo 的核心思路是**用 FFmpeg 原生 API 统一处理所有媒体类型**，避免引入冗余的第三方库：

- **光栅图像**（PNG / JPEG / BMP / WebP / AVIF / HEIF / TIFF / DNG 等 30+ 格式）全部走 FFmpeg avcodec 编解码，不依赖 stb_image 等独立图像库
- **矢量图**仅保留 nanosvg 处理 SVG（FFmpeg 本身不支持 SVG 解析），除此之外不引入任何图像库
- **音视频**通过 FFmpeg avformat 读写容器、avcodec 编解码、avfilter 处理滤镜
- **内存管理**全项目统一使用 `av_malloc` / `av_free`，杜绝 `malloc` / `stbi_image_free` 等混用导致的分配器不匹配
- **配置驱动**分层配置架构，用户显式设置优先，未指定参数自动从源文件属性回填，保证处理严谨可控
- **流拷贝兼容性**使用 `avformat_query_codec()` 预检，自动跳过输出容器不支持的流，与 FFmpeg CLI `-c copy` 行为一致
- **HEIF/HEIC 多瓦片**通过 `AVStreamGroupTileGrid` + `xstack` 滤镜自动拼接 iPhone 等设备的多瓦片图像
- **批量处理**内置批量引擎，JSON 清单驱动，支持全局默认参数 + 单文件独立覆盖

最终依赖：**FFmpeg + nanosvg + nlohmann/json**，无其他第三方运行时依赖。

## 架构

```
┌──────────────────────────────────────────────────────────────┐
│                    CLI / 未来 GUI 入口                         │
│           src/main.cpp  /  diag_main.cpp                      │
├──────────┬──────────┬──────────┬───────────────┬─────────────┤
│  config  │ media_io │transcoder│    batch      │    diag     │
│  配置模块 │ 媒体 I/O │ 转码引擎  │  批量处理引擎  │ FFmpeg 诊断  │
├──────────┴──────────┴──────────┴───────────────┴─────────────┤
│    FFmpeg (avcodec / avformat / avfilter / swscale /          │
│            swresample / avutil)                                │
│    + nanosvg (SVG 解析 / 光栅化)                               │
│    + nlohmann/json (配置序列化)                                │
└──────────────────────────────────────────────────────────────┘
```

五个核心模块，各司其职：

| 模块 | 职责 | 依赖 |
|------|------|------|
| `config` | 分层配置管理、编解码器/像素格式枚举、JSON 序列化 | FFmpeg + nlohmann/json |
| `media_io` | 媒体探测、可配置像素格式解码、统一编码、SVG 光栅化、bit-exact 流拷贝、HEIF 瓦片拼接 | FFmpeg + nanosvg |
| `transcoder` | 统一处理流水线：探测 → 配置解析 → 决策(COPY/ENCODE) → 执行 | FFmpeg + media_io + config |
| `batch` | 批量处理引擎：JSON 清单解析、通配符展开、参数分层合并、进度回调、输出路径规范化 | transcoder + nlohmann/json |
| `diag` | FFmpeg 版本/配置/硬解/VMAF 环境诊断 | FFmpeg |

## 统一处理流水线

```
输入文件
  │
  ├─ 1. Probe ─── media_probe() 获取源文件完整属性
  │     (编解码器 / 像素格式 / 位深 / 色彩空间 / ICC / 元数据)
  │     自动检测流组 (AVStreamGroupTileGrid) 处理 HEIF 多瓦片
  │
  ├─ 2. Resolve ─ 用户设置 > 全局默认 > 源属性回填
  │     "所选即所得，不选则保持原始"
  │
  ├─ 3. Decide ── 根据 Strategy 决策执行路径
  │     ├─ COPY   → bit-exact 流拷贝（零质量损失）
  │     │            自动预检容器兼容性，跳过不兼容流
  │     ├─ ENCODE → 解码 → 中间帧 → 编码（全参数可控）
  │     └─ AUTO   → 同格式兼容 → COPY；否则 → ENCODE
  │                 COPY 失败时自动降级到 ENCODE
  │
  └─ 4. Execute ─ 执行并返回 ProcessReport
        (实际路径 / 源属性 / 输出属性)
```

### 配置优先级

```
CLI 参数 (最高)  >  JSON 配置文件  >  全局默认  >  源文件属性采样 (兜底)
```

所有未指定参数（`nullptr` / `-1` / `AUTO`）均从源文件自动提取，保证**不选则保持原始**。

## 模块 API

### config — 配置模块

```cpp
// 处理策略
enum class Strategy { COPY = 0, ENCODE = 1, AUTO = 2 };

// 缩放模式
enum class ScaleMode { NONE = 0, FIT = 1, FILL = 2, STRETCH = 3 };

// 缩放算法
enum class ScaleAlgorithm { FAST_BILINEAR, BILINEAR, BICUBIC,
    BOX, GAUSS, SINC, LANCZOS, SPLINE, AREA };

// 编码参数（未指定字段从源回填）
struct CodecParams {
    const char* name = nullptr;       // 编码器名，nullptr = 自动
    const char* pixel_fmt = nullptr;  // 像素格式，nullptr = 与源一致
    int quality = -1;                 // 质量，-1 = 自动
    int bitrate = 0;
    int gop_size = 0;
    int thread_count = 0;
};

// 图片配置
struct ImageConfig {
    Strategy strategy = Strategy::AUTO;
    const char* intermediate_fmt = nullptr;  // 中间格式，nullptr = 与源一致
    ScaleMode scale_mode = ScaleMode::NONE;
    int scale_w = 0, scale_h = 0;
    ScaleAlgorithm scale_algorithm = ScaleAlgorithm::LANCZOS;
    CodecParams encode;
    bool preserve_icc = true;
    bool preserve_metadata = true;
};

// 全局配置
struct MediaGoConfig {
    ImageConfig image;
    VideoConfig video;   // Phase 3 预留
    AudioConfig audio;   // Phase 3 预留

    bool from_json(const char* path);
    bool to_json(const char* path) const;
    void print(FILE* fp = stdout) const;
};

// FFmpeg 环境枚举（供 UI/CLI 展示可选参数）
int config_list_codecs(CodecInfo* out, int max, bool encoders_only, bool video_only);
int config_list_pixel_fmts(PixelFmtInfo* out, int max);
```

### media_io — 统一媒体 I/O

```cpp
// 源文件属性
struct SourceInfo {
    char codec_name[64];
    int codec_id, width, height, bit_depth;
    AVPixelFormat pix_fmt;
    char pix_fmt_name[32], container[32];
    int color_space, color_range, color_primaries, color_trc;
    bool has_icc, has_alpha;
    int nb_streams;
    bool is_image;
};

// 探测（不解码像素，自动检测 Tile Grid 流组）
bool media_probe(const char* path, SourceInfo* info);

// 解码到指定像素格式（AV_PIX_FMT_NONE = 保留源格式）
// 自动处理 HEIF 多瓦片拼接、Bayer 原始格式警告
bool media_decode(const char* path, AVPixelFormat dst_fmt, AVFrame** frame_out);

// 统一编码（编码器/质量/格式由 ImageConfig 控制）
bool media_encode(const char* path, const AVFrame* frame, const ImageConfig& cfg);

// bit-exact 流拷贝（含 avformat_query_codec 容器兼容性预检）
bool media_stream_copy(const char* input, const char* output);

// SVG 光栅化（支持 fit/fill/stretch 缩放模式）
uint8_t* svg_rasterize_ex(const char* path, ScaleMode mode,
                           int w, int h, int* w_out, int* h_out);
void media_free(uint8_t* data);
```

### transcoder — 统一流水线

```cpp
struct TranscodeResult { bool ok; const char* error; };

struct ProcessReport {
    bool ok;
    const char* error;
    bool used_copy;          // true = stream copy；false = decode → encode
    char src_codec[64];
    int  src_width, src_height, src_bit_depth;
    char src_pix_fmt[32];
    char out_codec[64];
    int  out_width, out_height;
    char out_pix_fmt[32];
};

// 统一处理入口
TranscodeResult process_media(const char* input, const char* output,
                               const ImageConfig& cfg,
                               ProcessReport* report = nullptr);
```

### batch — 批量处理引擎

```cpp
enum class OutputStructure { FLAT, BY_TYPE };

struct BatchConfig {
    std::string output_dir;
    OutputStructure output_structure = OutputStructure::FLAT;
    ImageConfig defaults;  // 全局默认参数
};

struct BatchJob {
    std::string input;       // 支持通配符 (e.g. *.jpg, photo_*.png)
    std::string output;      // 可选，null 则自动生成
    std::string params_json; // 单文件参数覆盖（JSON 字符串）
};

struct JobResult {
    std::string input, output;
    bool ok;
    std::string error;
    ProcessReport report;
};

class JobManifest {
    int version = 1;
    BatchConfig config;
    std::vector<BatchJob> jobs;

    bool from_json(const char* path);
    bool to_json(const char* path) const;
};

class BatchProcessor {
    using ProgressCallback = void(*)(int current, int total, const char* file, void* user);

    void set_progress_callback(ProgressCallback cb, void* user = nullptr);
    bool process(const char* manifest_path);
    const std::vector<JobResult>& results() const;
    int success_count() const;
    int fail_count() const;
    static std::string make_output_path(const BatchConfig& cfg, const std::string& input);
};
```

**清单 JSON 格式：**

```json
{
  "version": 1,
  "output": {
    "dir": "./output",
    "structure": "flat"
  },
  "defaults": {
    "strategy": "encode",
    "scale_mode": "fit",
    "scale_w": 1920
  },
  "jobs": [
    { "input": "photo.jpg" },
    { "input": "*.png", "params": { "scale_w": 0 } },
    { "input": "video.mov", "output": "video.mp4", "params": { "strategy": "copy" } }
  ]
}
```

参数分层合并：`defaults`（全局默认） < `jobs[].params`（单文件覆盖）。

### diag — FFmpeg 诊断

```cpp
void diag_run_all();  // 完整报告：FFmpeg 版本、编译配置、硬件编解码器、VMAF
```

## 构建

### 当前状态

| 平台 | 编译器 | 构建系统 |
|------|--------|----------|
| Windows | MinGW-w64 g++ 8.1+ (C++17) | GNU Make |

### 依赖获取

| 依赖 | 获取方式 |
|------|---------|
| FFmpeg | `powershell -File scripts/setup_ffmpeg.ps1` 自动下载 BtbN win64 gpl-shared 预编译包 |
| nanosvg | 已内嵌于 `libs/nanosvg/`，纯头文件，无需额外操作 |
| nlohmann/json | 已内嵌于 `libs/nlohmann/`，单头文件，无需额外操作 |

### 快速开始

```powershell
# 1. 下载 FFmpeg 开发包
powershell -ExecutionPolicy Bypass -File scripts/setup_ffmpeg.ps1

# 2. 编译
mingw32-make

# 3. 复制运行时 DLL（当前使用 shared 构建）
Copy-Item libs/ffmpeg/bin/*.dll build/ -Force

# 4. 验证
./build/MediaGo.exe info
```

### 构建目标

| 命令 | 产物 | 说明 |
|------|------|------|
| `mingw32-make` | `build/MediaGo.exe` | 主程序 |
| `mingw32-make diag` | `build/diag.exe` | 独立诊断工具 |
| `mingw32-make clean` | — | 清理 build/ |
| `mingw32-make info` | — | 检查 FFmpeg 头文件/库就位 |

## 使用

### 核心命令

```powershell
# 统一媒体转换（图片格式互转 / 容器转封装）
MediaGo convert input.png output.webp
MediaGo convert input.jpg output.png --quality 85
MediaGo convert input.jpg output.jpg --strategy copy      # bit-exact 流拷贝
MediaGo convert input.png output.jpg --codec mjpeg --pixfmt yuvj420p
MediaGo convert input.svg output.png --scale 1920x1080 --scalemode fit
MediaGo convert input.mkv output.mp4 --strategy copy       # 容器转封装

# 批量处理（JSON 清单驱动，支持通配符 + 参数分层合并）
MediaGo batch manifest.json

# 源文件属性探测
MediaGo probe photo.heic

# 列出可用编解码器
MediaGo codecs                   # 视频编码器
MediaGo codecs audio             # 音频编码器

# 列出可用像素格式
MediaGo pixfmts

# 配置管理
MediaGo config                   # 查看当前默认配置
MediaGo config export my.json    # 导出配置
MediaGo config load my.json      # 加载配置
```

### convert 选项

| 选项 | 值 | 说明 |
|------|-----|------|
| `--strategy` | `copy` / `encode` / `auto` | 处理策略（默认: auto） |
| `--codec` | `<name>` | 指定编码器（如 `libx265`, `png`, `mjpeg`） |
| `--quality` | `<1-100>` | 质量参数 |
| `--pixfmt` | `<fmt>` | 目标像素格式（如 `yuv420p`, `rgb24`） |
| `--intermediate` | `<fmt>` | 中间像素格式（默认与源一致） |
| `--scale` | `<WxH>` | 缩放尺寸 |
| `--scalemode` | `fit` / `fill` / `stretch` | 缩放模式 |
| `--scalealgo` | `lanczos` / `bilinear` / `bicubic` / `area` | 缩放算法 |
| `--no-icc` | — | 丢弃 ICC 色彩配置文件 |
| `--no-meta` | — | 丢弃元数据 |

### 向后兼容命令

```powershell
MediaGo img    in.png out.jpg    # = convert --strategy auto
MediaGo remux  in.mkv out.mp4    # = convert --strategy copy
MediaGo load   photo.jpg         # 解码到 RGBA 并显示尺寸
MediaGo svg    icon.svg 256 256  # SVG 光栅化
```

## 项目结构

```
MediaGo/
├── src/
│   ├── main.cpp                 # CLI 入口（命令分发）
│   ├── diag_main.cpp            # 诊断工具入口
│   └── core/
│       ├── config.h             # 配置数据结构与枚举接口
│       ├── config.cpp           # JSON 读写 + FFmpeg 编解码器/像素格式枚举
│       ├── media_io.h           # 统一媒体 I/O 接口
│       ├── media_io.cpp         # 探测 / 解码(含 Tile Grid 拼接) / 编码 / 流拷贝 / SVG
│       ├── transcoder.h         # 统一流水线接口
│       ├── transcoder.cpp       # probe → resolve → decide → execute
│       ├── batch.h              # 批量处理引擎接口
│       ├── batch.cpp            # JSON 清单 / 通配符展开 / 参数合并 / 批量执行
│       ├── diag.h               # FFmpeg 诊断接口
│       └── diag.cpp             # 诊断实现
├── libs/
│   ├── nanosvg/                 # 源码级依赖（zlib 许可）
│   │   ├── nanosvg.h            # SVG 解析
│   │   └── nanosvgrast.h        # 软件光栅化
│   └── nlohmann/
│       └── json.hpp             # JSON 序列化（MIT 许可，单头文件）
├── scripts/
│   └── setup_ffmpeg.ps1         # FFmpeg 开发包下载脚本
├── Makefile                     # MinGW 构建
├── .gitignore
└── README.md
```

## FFmpeg 模块

| 库 | 使用模块 | 用途 |
|----|---------|------|
| libavcodec | media_io, transcoder, config | 图像/音视频编解码、编解码器枚举 |
| libavformat | media_io, transcoder | 容器读写、流探测、格式检测、流组(Tile Grid)遍历 |
| libavutil | media_io, transcoder, config | 内存管理、像素格式、帧操作、opt 设置 |
| libswscale | media_io, transcoder | 像素格式转换、缩放 |
| libswresample | transcoder | 音频重采样 |
| libavfilter | media_io, diag | xstack 滤镜（HEIF 瓦片拼接）、VMAF 检测 |

## 路线图

### 第一阶段：跨平台构建

> 将 MinGW Makefile 迁移到 CMake

- CMake 统一 Windows / macOS / Linux 三平台构建
- FFmpeg 在所有主流平台都有预编译包
- nanosvg / nlohmann/json 纯头文件，零移植成本

### 第二阶段：静态链接

> 单个可执行文件，无需用户单独安装 FFmpeg

- FFmpeg 静态链接到 MediaGo
- 拷贝即用，零外部依赖

### 第三阶段：完整转码引擎

> re-encode 模式 + 编解码器选择策略

- 视频/音频完整编解码管线
- 软件/硬件编码器自动选择与优先级策略
- 视频滤镜链：缩放、裁剪、帧率转换、去隔行

### 第四阶段：媒体元数据

> EXIF / ID3 / QuickTime / Matroska 元数据读写

- 基于 FFmpeg `AVDictionary` API
- 零额外依赖

### 第五阶段：图形界面

> Qt 6 跨平台 GUI

- 核心模块与界面完全解耦
- 文件选择、参数配置、进度条、预览

## 许可

| 组件 | 许可 |
|------|------|
| FFmpeg | LGPL / GPL（取决于编译选项） |
| nanosvg | zlib |
| nlohmann/json | MIT |
| MediaGo 自身 | GPL |
