# MediaGo

跨平台媒体处理工具，基于 FFmpeg + nanosvg 的轻量级音视频与图像处理方案。

## 设计理念

MediaGo 的核心思路是**用 FFmpeg 统一处理所有媒体类型**，避免引入冗余的第三方库：

- **光栅图像**（PNG / JPEG / BMP / WebP / AVIF / HEIF / TIFF 等 30+ 格式）全部走 FFmpeg avcodec 编解码，不依赖 stb_image 等独立图像库
- **矢量图**仅保留 nanosvg 处理 SVG（FFmpeg 本身不支持 SVG 解析），除此之外不引入任何图像库
- **音视频**通过 FFmpeg avformat 读写容器、avcodec 编解码、avfilter 处理滤镜
- **内存管理**全项目统一使用 `av_malloc` / `av_free`，杜绝 `malloc` / `stbi_image_free` 等混用导致的分配器不匹配

最终依赖极简：**只有 FFmpeg + nanosvg 两个头文件**，没有其他第三方运行时依赖。

## 架构

```
┌───────────────────────────────────────────────┐
│                CLI / 未来 GUI 入口              │
│         src/main.cpp  /  diag_main.cpp         │
├───────────────┬───────────────┬───────────────┤
│   media_io    │  transcoder   │     diag      │
│  统一媒体 I/O  │  转码引擎     │   FFmpeg 诊断  │
├───────────────┴───────────────┴───────────────┤
│              FFmpeg (avcodec / avformat         │
│           avfilter / swscale / swresample)      │
│          + nanosvg (SVG 解析 / 光栅化)          │
└───────────────────────────────────────────────┘
```

三个核心模块，各司其职：

| 模块 | 职责 | 依赖 |
|------|------|------|
| `media_io` | 图像解码（任意格式→RGBA）、PNG/JPEG 编码、SVG 光栅化 | FFmpeg + nanosvg |
| `transcoder` | 容器转换（remux）、图片格式互转 | FFmpeg + media_io |
| `diag` | FFmpeg 版本/配置/硬解/VMAF 环境诊断 | FFmpeg |

## 模块 API

### media_io — 统一媒体 I/O

所有图像操作统一走 RGBA 像素缓冲区，调用方无需关心原始格式。

```cpp
// 解码任意光栅图像 → RGBA
uint8_t* media_load(const char* path, int* w, int* h);

// 编码 RGBA → PNG / JPEG
bool media_save_png(const char* path, int w, int h, const uint8_t* data);
bool media_save_jpg(const char* path, int w, int h, const uint8_t* data, int quality);

// SVG 解析 + 光栅化 → RGBA
uint8_t* svg_rasterize(const char* path, int w, int h);

// 释放以上函数返回的内存
void media_free(uint8_t* data);
```

`media_load` 内部通过 `avformat_open_input` 自动探测格式 → `avcodec` 解码 → `sws_scale` 转 RGBA。只要 FFmpeg 支持的格式即可解码，无需白名单。

### transcoder — 转码引擎

```cpp
struct TranscodeResult { bool ok; const char* error; };

// 容器格式转换（流复制，不重新编码）
TranscodeResult transcode_media(const char* input, const char* output);

// 图片格式转换（根据扩展名自动选择编码路径）
TranscodeResult convert_image(const char* input, const char* output);
```

- `transcode_media`：打开输入 → 探测流信息 → 根据输出扩展名创建容器 → 流复制所有 track → 写文件。支持 MP4 / MKV / WebM / AVI / FLV 等。
- `convert_image`：PNG ↔ JPEG 直接走 `media_save`；其他格式回退到 `transcode_media` 处理。

### diag — FFmpeg 诊断

```cpp
void diag_run_all();  // 完整报告：FFmpeg 版本、编译配置、硬件编解码器、VMAF
```

## 构建

### 当前状态

| 平台 | 编译器 | 构建系统 |
|------|--------|----------|
| Windows | MinGW-w64 g++ 8.1+ (C++17) | GNU Make |

FFmpeg 通过 `scripts/setup_ffmpeg.ps1` 自动下载 [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) 的 win64 gpl-shared 预编译包（含 libx264 / libx265 / libvmaf），解压到 `libs/ffmpeg/`。

nanosvg 是纯头文件库（`libs/nanosvg/nanosvg.h` + `nanosvgrast.h`），直接 `#include` 编译，无需额外下载。

### 快速开始

```powershell
# 1. 下载 FFmpeg 开发包（头文件 + 导入库）
powershell -ExecutionPolicy Bypass -File scripts/setup_ffmpeg.ps1

# 2. 编译
mingw32-make

# 3. 验证 FFmpeg 环境
mingw32-make run   # 等价于 build/MediaGo.exe info
```

### 构建目标

| 命令 | 产物 | 说明 |
|------|------|------|
| `mingw32-make` | `build/MediaGo.exe` | 主程序 |
| `mingw32-make diag` | `build/diag.exe` | 独立诊断工具 |
| `mingw32-make clean` | — | 清理 build/ |
| `mingw32-make info` | — | 检查 FFmpeg 头文件/库就位 |

## 使用

```powershell
# 环境诊断（FFmpeg 版本、可用硬编解码器、VMAF）
MediaGo info

# 解码任意图片（自动检测格式，输出宽高）
MediaGo load photo.jpg

# SVG → 指定尺寸 RGBA 光栅化
MediaGo svg icon.svg 256 256

# 图片格式转换（根据扩展名自动选择编码器）
MediaGo img input.png output.jpg

# 容器格式转换（流复制，快速无损）
MediaGo remux input.mkv output.mp4
```

## 项目结构

```
MediaGo/
├── src/
│   ├── main.cpp                # CLI 入口（命令分发）
│   ├── diag_main.cpp           # 诊断工具入口
│   └── core/
│       ├── media_io.h          # 统一媒体 I/O 接口
│       ├── media_io.cpp        # FFmpeg 光栅图 + nanosvg 矢量图实现
│       ├── transcoder.h        # 转码引擎接口
│       ├── transcoder.cpp      # remux + 图片格式转换实现
│       ├── diag.h              # FFmpeg 诊断接口
│       └── diag.cpp            # 诊断实现
├── libs/
│   └── nanosvg/                # 源码级依赖（zlib 许可）
│       ├── nanosvg.h           # SVG 解析
│       └── nanosvgrast.h       # 软件光栅化
├── scripts/
│   └── setup_ffmpeg.ps1        # FFmpeg 开发包下载
├── Makefile                    # MinGW 构建
└── .gitignore
```

`.gitignore` 已排除 `build/`（编译产物）、`libs/ffmpeg/`（预编译包）、`tests/`（内部测试），仓库只保留源码和项目配置。

## FFmpeg 模块

本项目链接 6 个 FFmpeg 库，各模块按需使用：

| 库 | 被哪些模块使用 | 用途 |
|----|---------------|------|
| libavcodec | media_io, transcoder | 图像编解码、音频编解码 |
| libavformat | media_io, transcoder | 容器读写、流探测、格式检测 |
| libavutil | media_io, transcoder | 内存 (`av_malloc`)、像素格式、帧操作 |
| libswscale | media_io | 像素格式转换（任意→RGBA） |
| libswresample | transcoder | 音频重采样 |
| libavfilter | — (预留) | 滤镜图（PSNR/SSIM/VMAF 已通过测试验证可用） |

## 路线图

当前已完成基础架构和核心功能，以下是后续开发路径。

### 第一阶段：跨平台构建

> **从这里继续：将 MinGW Makefile 迁移到 CMake**

- 当前 Makefile 仅支持 Windows + MinGW。CMake 可以统一 Windows / macOS / Linux 三平台的构建流程
- FFmpeg 在所有主流平台都有预编译包（BtbN/FFmpeg-Builds 提供 Windows，Homebrew 提供 macOS，apt 提供 Linux），CMake 的 `find_package` 可以自动定位
- nanosvg 是纯 C 头文件，零移植成本
- 改动量：新增 `CMakeLists.txt`，`src/core/` 代码无需修改

```
# 迁移后的典型构建流程：
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 第二阶段：独立可执行文件

> **目标：一个 exe / app 包即可分发，无需用户单独安装 FFmpeg**

- FFmpeg 静态链接：将 `avcodec` / `avformat` 等库编译进 `MediaGo`，不再依赖外部 DLL
- BtbN 同时提供 gpl-shared 和 gpl 静态版本，只需在 CMake 中配置静态链接即可
- nanosvg 已是源码编译，天然静态
- 最终产物是单个可执行文件，拷贝即用

### 第三阶段：转码引擎

> **当前只实现了 remux（流复制），需要增加完整的编解码转码**

- 新增 `transcode_media` 的 re-encode 模式：指定输出编码器和参数，将输入流解码后重新编码
- 拆分 codec 匹配逻辑：自动选择最佳编码器（软件/硬件优先策略）
- 支持视频滤镜链：缩放、裁剪、帧率转换、去隔行

### 第四阶段：媒体元数据

- 新增 `core/metadata` 模块：读取/写入媒体文件的元数据（标题、作者、封面、章节等）
- FFmpeg 的 `AVDictionary` API 直接支持，无需额外依赖
- 支持 ID3 (MP3)、EXIF (JPEG)、QuickTime (MP4/MOV)、Matroska (MKV/WebM) 等元数据格式

### 第五阶段：图形界面

> **CLI 稳定后，包装为跨平台 GUI 应用**

- 推荐 Qt 6（LGPL 许可，与 GPL 兼容）：`QProcess` 调用 CLI 后端，或直接链接核心模块
- 核心层 `media_io` / `transcoder` 与界面完全解耦，可单独编译为静态库供 GUI 调用
- GUI 负责：文件选择、格式/参数配置、进度条、预览

### 格式支持扩展

- `media_save` 当前只实现了 PNG/JPEG 编码。后续可补充 WebP、AVIF 编码（FFmpeg codec 已内置）
- SVG 光栅化输出当前只返回 RGBA 内存，可增加直接保存为 PNG 的便捷接口

## 许可

| 组件 | 许可 |
|------|------|
| FFmpeg | LGPL / GPL（取决于编译选项） |
| nanosvg | zlib |
| MediaGo 自身 | GPL |

`libs/nanosvg/` 是源码级依赖，仅两个头文件约 4800 行，编译时直接嵌入。FFmpeg 不纳入仓库，通过脚本下载预编译包。
