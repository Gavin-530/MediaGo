# MediaGo

跨平台专业媒体处理工具，基于原生 FFmpeg 管线的音视频、图像处理方案。支持 **CLI 命令行** 和 **GUI 浏览器界面**，适用于影视后期、数字资产管理、自动化转码流水线等工业场景。

## 设计理念

| 原则 | 说明 |
|------|------|
| **默认无损** | 不指定编码器即 stream copy，bit-exact 数字级无损 |
| **所选即所得** | 用户显式指定参数才生效，不做自主推断或自动降级 |
| **原生 FFmpeg** | 标准 `avformat → avcodec → avfilter → avcodec → avformat` 五阶段管线 |
| **行业标准输出** | Lanczos 缩放、CRF 质量控制、色彩元数据完整传递 |
| **单一路径** | 整个项目只有一条 TranscodeEngine 处理管线 |
| **前后端分离** | C++ HTTP 服务层 + Vue 3 前端，大企业开发范式 |

## 架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         前端 GUI                                │
│               Vue 3 + TypeScript + Element Plus                 │
│                    http://localhost:5173                        │
└──────────────────────────┬──────────────────────────────────────┘
                           │  REST API + SSE
┌──────────────────────────▼──────────────────────────────────────┐
│                    HTTP 服务层 (MediaGoServer)                   │
│              cpp-httplib · REST · SSE 进度推送                  │
│                    http://localhost:9527                        │
├─────────────────────────────────────────────────────────────────┤
│  POST /api/upload             │  多文件上传，保存临时目录               │
│  POST /api/batch              │  提交 JSON 清单，异步转码               │
│  GET  /api/progress/:task_id  │  SSE 实时进度推送                      │
│  GET  /api/probe              │  媒体属性探测                          │
│  GET  /api/codecs             │  枚举可用编解码器                      │
│  GET  /api/pixfmts            │  枚举可用像素格式                      │
│  POST /api/encoder-params     │  按编码器查询私有参数（码率控制等）    │
│  POST /api/audio-encoder-params│ 按音频编码器查询私有参数               │
│  GET  /api/history            │  查询处理历史                          │
│  POST /api/open-folder        │  打开资源管理器目录                    │
└──────────────────────────┬──────────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────────┐
│                     CLI (src/main.cpp)                          │
│                                                                  │
│  convert ──→ transcode_run() ──→ TranscodeEngine                │
│  batch   ──→ BatchProcessor ───────────┘                        │
│  probe   ──→ media_probe()                                      │
│  codecs  ──→ config_list_codecs()                               │
│  pixfmts ──→ config_list_pixel_fmts()                           │
└──────────────────────────┬──────────────────────────────────────┘
                           │
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
│   ├── main.cpp                      CLI 入口（convert/batch/probe/codecs/pixfmts）
│   ├── main_server.cpp               服务入口（HTTP REST API）
│   ├── core/
│   │   ├── config.h / .cpp           缩放算法枚举、编解码器/像素格式运行时枚举
│   │   ├── transcode_config.h        VideoConfig / AudioConfig / TranscodeConfig
│   │   ├── transcode_engine.h / .cpp FFmpeg 原生转码引擎
│   │   ├── batch.h / .cpp            JSON 清单批量处理
│   │   └── media_io.h / .cpp         媒体探测 (probe) + SVG 光栅化
│   └── server/
│       └── http_server.h / .cpp      HTTP 服务层（路由 + SSE + 任务管理）
├── frontend/                         前端工程（Vue 3 + TypeScript + Vite）
│   ├── package.json
│   ├── vite.config.ts                API 代理 /api → 127.0.0.1:9527
│   ├── index.html
│   └── src/
│       ├── main.ts                   应用入口（Element Plus + Router）
│       ├── App.vue                   侧边栏布局 + 导航
│       ├── router/index.ts           路由：/(批量) + /probe(探测)
│       ├── api/index.ts              Axios API 封装
│       ├── composables/useSSE.ts     EventSource SSE 进度流
│       ├── components/
│       │   ├── FileUploader.vue     拖拽上传 + 文件列表
│       │   ├── ProgressPanel.vue    进度条 + SSE 实时更新
│       │   └── ProbeResult.vue      媒体属性描述列表
│       ├── views/
│       │   ├── BatchPage.vue        批量处理主页面
│       │   └── ProbePage.vue        媒体探测页面
│       └── styles/
│           └── main.css
├── devtools/                         开发工具（诊断程序，非发布产品）
│   ├── CMakeLists.txt
│   ├── diag.h / .cpp                 FFmpeg 版本/硬件编解码器/VMAF 检查
│   └── diag_main.cpp                 诊断程序入口
├── libs/
│   ├── ffmpeg/                       FFmpeg 开发包（预编译，Windows MinGW）
│   ├── cpp-httplib/                  HTTP 单头文件库（MIT 许可）
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
| Node.js | >= 18 | 前端开发环境（若仅用 CLI 可选） |

### 构建 C++ 后端

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
├── MediaGo.exe             CLI 主程序
├── MediaGoServer.exe       HTTP 服务端（GUI 后端）
└── *.dll                   FFmpeg 运行时库（Windows）
```

### 构建前端（GUI）

```powershell
cd frontend
npm install          # 安装依赖（仅首次）
npm run dev          # 启动开发服务器（localhost:5173）
npm run build        # 生产构建（输出到 frontend/dist/）
```

### 启动完整服务

```powershell
# 终端 1：启动 C++ 后端
cd build
.\MediaGoServer.exe --port 9527

# 终端 2：启动前端开发服务器
cd frontend
npm run dev
```

打开浏览器访问 `http://localhost:5173/` 即可使用 GUI。

> **生产模式**：`npm run build` 后，将 `frontend/dist/` 作为 `--web-root` 参数传给服务端：
> ```powershell
> .\MediaGoServer.exe --port 9527 --web-root ..\frontend\dist
> ```
> 然后直接访问 `http://127.0.0.1:9527/` 即可（不需要前端开发服务器）。

## 使用

### CLI 命令

```
MediaGo <command> [args]
```

#### convert — 单文件转换

```powershell
MediaGo convert <input> <output> [options]
```

默认不指定 codec = stream copy（bit-exact 无损）。

**选项：**

| 选项 | 参数 | 说明 |
|------|------|------|
| `--vcodec` | `<name>` | 视频编码器，默认 copy |
| `--acodec` | `<name>` | 音频编码器，默认 copy |
| `--rate-control` | `<mode>` | 码率控制：crf / cqp / abr / cbr / vbr 等 |
| `--crf` | `<0-51>` | CRF 质量控制 |
| `--bitrate` | `<bps>` | 码率 |
| `--maxrate` | `<bps>` | 最大码率 (VBV) |
| `--bufsize` | `<bps>` | 缓冲大小 (VBV) |
| `--min-qp` | `<q>` | 最小 QP |
| `--max-qp` | `<q>` | 最大 QP |
| `--preset` | `<name>` | 编码器预设 |
| `--tune` | `<name>` | 编码器调优 |
| `--scale` | `<WxH>` | 缩放尺寸 |
| `--fps` | `<rate>` | 目标帧率 |
| `--format` | `<name>` | 输出容器格式 |
| `--opts` | `<json>` | 编码器私有参数 JSON |
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

#### batch — 批量处理

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
      "rate_control": "crf",
      "crf": 23,
      "bitrate": 0,
      "maxrate": 0,
      "bufsize": 0,
      "min_qp": -1,
      "max_qp": -1,
      "width": 0,
      "height": 0,
      "fps": 0.0,
      "keep_aspect": true,
      "preset": "",
      "tune": "",
      "opts_json": null
    },
    "audio": {
      "codec": "",
      "rate_control": "",
      "bitrate": 0,
      "quality": 0,
      "opts_json": null
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
        "rate_control": "crf",
        "crf": 18,
        "bitrate": 0,
        "maxrate": 5000000,
        "bufsize": 10000000,
        "min_qp": 10,
        "max_qp": 40,
        "width": 1920,
        "height": 1080,
        "fps": 24,
        "keep_aspect": true,
        "preset": "medium",
        "tune": "film",
        "opts_json": null
      },
      "audio": {
        "copy": false,
        "codec": "aac",
        "rate_control": "cbr",
        "bitrate": 256000,
        "quality": 0,
        "opts_json": null
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
| `video.rate_control` | string | 码率控制模式：crf / cqp / abr / cbr / vbr / constqp 等 |
| `video.crf` | int | CRF 质量控制 |
| `video.bitrate` | int | 视频码率 |
| `video.maxrate` | int | 最大码率 (VBV) |
| `video.bufsize` | int | 缓冲大小 (VBV) |
| `video.min_qp` | double | 最小 QP |
| `video.max_qp` | double | 最大 QP |
| `video.width` / `height` | int | 缩放尺寸 |
| `video.fps` | double | 目标帧率 |
| `video.keep_aspect` | bool | 保持宽高比（默认 true） |
| `video.preset` | string | 编码器预设 |
| `video.tune` | string | 编码器调优 |
| `video.opts_json` | string | 编码器私有参数 JSON（av_opt_set） |
| `audio.copy` | bool | `true` = 音频流拷贝 |
| `audio.codec` | string | 音频编码器 |
| `audio.rate_control` | string | 码率控制模式：cbr / abr / vbr / vbr_quality 等 |
| `audio.bitrate` | int | 音频码率 |
| `audio.quality` | double | 音频质量参数 |
| `audio.opts_json` | string | 编码器私有参数 JSON（av_opt_set） |

#### probe — 源文件属性探测

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

#### codecs — 运行时编解码器枚举

```powershell
MediaGo codecs          # 视频编码器
MediaGo codecs audio    # 音频编码器
```

#### pixfmts — 像素格式枚举

```powershell
MediaGo pixfmts
```

### HTTP API（GUI 后端）

服务端启动后，提供以下 REST API：

| 方法 | 路径 | 说明 |
|------|------|------|
| `GET` | `/api/health` | 健康检查 |
| `GET` | `/api/codecs` | 编解码器列表（?type=audio 筛选） |
| `GET` | `/api/pixfmts` | 像素格式列表 |
| `GET` | `/api/probe?path=xxx` | 探测媒体属性 |
| `POST` | `/api/upload` | 多文件上传（multipart/form-data） |
| `POST` | `/api/batch` | 提交批量转码（JSON 清单） |
| `GET` | `/api/progress/:task_id` | SSE 实时进度推送 |
| `POST` | `/api/encoder-params` | 查询编码器专属参数 `{"codec":"libx264"}` |
| `POST` | `/api/audio-encoder-params` | 查询音频编码器参数 `{"codec":"aac"}` |
| `GET` | `/api/history` | 处理历史记录 |
| `POST` | `/api/open-folder` | 打开资源管理器目录 `{"dir":"path"}` |

**curl 示例：**

```powershell
# 健康检查
curl http://127.0.0.1:9527/api/health

# 探测文件属性
curl "http://127.0.0.1:9527/api/probe?path=C:/Videos/sample.mp4"

# 提交批量转码
curl -X POST http://127.0.0.1:9527/api/batch -H "Content-Type: application/json" -d '{"output":{"dir":"./output"},"jobs":[{"input":"sample.mp4","video":{"width":1920,"height":1080}}]}'

# SSE 进度监控
curl -N http://127.0.0.1:9527/api/progress/abc123

# 查询编码器参数
curl -X POST http://127.0.0.1:9527/api/encoder-params -H "Content-Type: application/json" -d '{"codec":"libx264"}'

# 查询音频编码器参数
curl -X POST http://127.0.0.1:9527/api/audio-encoder-params -H "Content-Type: application/json" -d '{"codec":"aac"}'
```

### GUI 页面

| 页面 | 路径 | 功能 |
|------|------|------|
| 批量处理 | `/` | 拖拽上传 → 配置编码参数（基础处理 + 高级选项） → 异步转码 → SSE 实时进度 |
| 媒体探测 | `/probe` | 输入路径 → 查看编解码器、分辨率、颜色空间等属性 |

批量处理页面支持：

- **视频编码**：选择编码器后自动拉取该编码器支持的码率控制模式（CRF/CQP/ABR/CBR/VBR 等），按编码器真实支持罗列
- **基础处理**：缩放 / 帧率 / 像素格式 / GOP 等编码器无关参数，始终可见
- **高级选项**（折叠）：线程数 + 编码器专属参数（profile / level / bframes 等 FFmpeg 原生参数）
- **音频编码**：独立码率控制（CBR/ABR/VBR/VBR-Quality 等），按编码器区分
- **图片编码**：mjpeg / libwebp 等图片编码器独立于视频列表展示

## 核心 API

### TranscodeConfig（`src/core/transcode_config.h`）

```cpp
struct VideoConfig {
    const char* codec = nullptr;   // nullptr 或 "copy" = 流拷贝
    const char* rate_control = nullptr; // crf / cqp / abr / cbr / vbr / constqp 等
    int crf = -1;                  // CRF (-1 = 编码器默认)
    int64_t bitrate = 0;           // 码率
    int64_t maxrate = 0;           // 最大码率 (VBV)
    int64_t bufsize = 0;           // 缓冲大小 (VBV)
    double min_qp = -1;            // 最小 QP (-1 = 默认)
    double max_qp = -1;            // 最大 QP (-1 = 默认)
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
    const char* opts_json = nullptr; // 编码器私有参数 JSON（av_opt_set）
};

struct AudioConfig {
    const char* codec = nullptr;   // nullptr 或 "copy" = 流拷贝
    const char* rate_control = nullptr; // cbr / abr / vbr / vbr_quality 等
    int64_t bitrate = 0;
    double quality = 0;            // 音频质量参数（编码器相关）
    int sample_rate = 0;
    const char* channel_layout = nullptr;
    const char* opts_json = nullptr; // 编码器私有参数 JSON（av_opt_set）
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

### MediaGoServer（`src/server/http_server.h`）

```cpp
class MediaGoServer {
public:
    MediaGoServer();
    ~MediaGoServer();

    bool start(int port = 9527, const char* web_root = nullptr);
    void stop();
    bool is_running() const;
};

// 用法
//   MediaGoServer svr;
//   svr.start(9527, "./frontend/dist");  // 生产模式
//   svr.start(9527);                     // 仅 API 模式
```

### BatchProcessor（`src/core/batch.h`）

```cpp
struct BatchJobItem {
    std::string input;            // 必填，支持通配符
    std::string output;           // 可选
    std::string video_codec;      // 空字符串 = 拷贝
    std::string video_rate_control; // crf / cqp / abr / cbr / vbr 等
    int video_crf = -1;
    int64_t video_bitrate = 0;
    int64_t video_maxrate = 0;
    int64_t video_bufsize = 0;
    double video_min_qp = -1;
    double video_max_qp = -1;
    int video_width = 0;
    int video_height = 0;
    double video_fps = 0.0;
    bool video_keep_aspect = true;
    bool video_copy = false;
    std::string video_preset;
    std::string video_tune;
    std::string video_opts_json;   // 视频编码器私有参数 JSON
    std::string audio_codec;      // 空字符串 = 拷贝
    std::string audio_rate_control; // cbr / abr / vbr / vbr_quality 等
    int64_t audio_bitrate = 0;
    double audio_quality = 0;
    bool audio_copy = false;
    std::string audio_opts_json;   // 音频编码器私有参数 JSON
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
    bool is_image;          // true = 图片编码器（mjpeg / libwebp 等）
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

## 技术栈

| 层 | 技术 | 说明 |
|----|------|------|
| 转码引擎 | C++17 + FFmpeg | 五阶段原生管线 |
| HTTP 服务 | cpp-httplib | 单头文件零依赖 |
| 前端框架 | Vue 3 + TypeScript | 组件化 GUI |
| UI 组件库 | Element Plus | 企业级设计规范 |
| 构建工具 | Vite | 极速开发体验 |
| 桌面打包 | Tauri 2.x（规划中） | 轻量跨平台桌面 App |

## 许可

| 组件 | 许可 |
|------|------|
| MediaGo 自身 | GPL |
| FFmpeg | LGPL / GPL（取决于编译选项） |
| cpp-httplib | MIT |
| nanosvg | zlib |
| nlohmann/json | MIT |
| Vue 3 / Element Plus | MIT |
