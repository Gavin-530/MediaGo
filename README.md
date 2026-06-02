# MediaGo

跨平台媒体处理工具，基于 FFmpeg + nanosvg 的轻量级音视频/图像处理方案。

## 架构

```
┌─────────────────────────────────────────────┐
│                  CLI 入口                    │
│         src/main.cpp  /  diag_main.cpp       │
├─────────────────────────────────────────────┤
│   media_io      │  transcoder   │   diag     │
│   统一媒体 I/O   │  转码引擎     │  诊断工具   │
├─────────────────────────────────────────────┤
│              FFmpeg (avcodec/avformat/       │
│             avfilter/swscale/swresample)      │
│           +  nanosvg (SVG 解析/光栅化)        │
└─────────────────────────────────────────────┘
```

- **光栅图**：全部走 FFmpeg avcodec，覆盖 PNG / JPEG / BMP / WebP / AVIF / HEIF / TIFF 等 FFmpeg 支持的所有格式
- **矢量图**：nanosvg 负责 SVG 解析和光栅化
- **音视频**：FFmpeg avformat 容器读写 + avcodec 编解码
- **质量评估**：FFmpeg avfilter 滤镜管线（PSNR / SSIM / VMAF）
- **内存统一**：`av_malloc` / `av_free`，杜绝分配器混用

## 模块 API

### media_io — 统一媒体 I/O

| 函数 | 说明 |
|------|------|
| `media_load(path, &w, &h)` | 读取图像文件，解码为 RGBA 像素。返回 `uint8_t*`，用 `media_free()` 释放 |
| `media_save_png(path, w, h, data)` | 保存 RGBA 像素为 PNG |
| `media_save_jpg(path, w, h, data, quality)` | 保存 RGBA 像素为 JPEG，quality 1–100 |
| ` media_free(data)` | 释放 `media_load()` / `svg_rasterize()` 返回的内存 |
| `svg_rasterize(path, w, h)` | 解析 SVG 并光栅化为 RGBA |

`media_load` 通过 FFmpeg codec 自动检测格式，支持 FFmpeg 所有可解码的光栅图像格式。

### transcoder — 转码引擎

| 函数 | 说明 |
|------|------|
| `transcode_media(in, out)` | 容器格式转换（流复制 remux）。根据输出扩展名自动选择容器，支持 MP4 / MKV / WebM / AVI / FLV 等 |
| `convert_image(in, out)` | 图片格式转换。PNG ↔ JPEG 直接走 `media_save`；其他格式回退到 `transcode_media` |

### diag — FFmpeg 诊断

| 函数 | 说明 |
|------|------|
| `diag_run_all()` | 完整诊断：FFmpeg 版本、构建配置、硬件编解码器列表、VMAF 可用性 |

## 构建

### 依赖

- **MinGW-w64** (g++ 8.1+，C++17)
- **FFmpeg** 完整开发包（BtbN gpl-shared 预编译版，含 libx264 / libx265 / libvmaf 等）

### 快速开始

```powershell
# 1. 下载 FFmpeg 开发包到 libs/ffmpeg/
powershell -ExecutionPolicy Bypass -File scripts/setup_ffmpeg.ps1

# 2. 编译主程序
mingw32-make

# 3. 运行诊断
mingw32-make run
```

`scripts/setup_ffmpeg.ps1` 从 [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds) 下载最新 win64 gpl-shared 版本，解压到 `libs/ffmpeg/`，包含头文件和导入库。

### 构建目标

| 命令 | 产物 | 说明 |
|------|------|------|
| `mingw32-make` | `build/MediaGo.exe` | 主程序 |
| `mingw32-make diag` | `build/diag.exe` | FFmpeg 环境诊断工具 |
| `mingw32-make clean` | — | 清理 build/ |
| `mingw32-make info` | — | 检查 FFmpeg 头文件和库是否就位 |

## 使用

```powershell
# FFmpeg 环境诊断（版本、硬件编解码器、VMAF）
MediaGo info

# 解码图片（自动检测格式）
MediaGo load photo.jpg

# SVG 光栅化到指定尺寸
MediaGo svg icon.svg 256 256

# 图片格式转换
MediaGo img input.png output.jpg

# 容器格式转换（流复制，不重新编码）
MediaGo remux input.mkv output.mp4
```

## 项目结构

```
MediaGo/
├── src/
│   ├── main.cpp                # 主程序入口（CLI 命令分发）
│   ├── diag_main.cpp           # 诊断工具入口
│   └── core/
│       ├── media_io.h / .cpp   # 统一媒体 I/O（FFmpeg 光栅图 + nanosvg 矢量图）
│       ├── transcoder.h / .cpp # 转封装 + 图片格式转换
│       └── diag.h / .cpp       # FFmpeg 诊断
├── libs/
│   └── nanosvg/                # nanosvg 单头文件库（zlib 许可）
│       ├── nanosvg.h           # SVG 解析器
│       └── nanosvgrast.h       # 软件光栅化器
├── scripts/
│   └── setup_ffmpeg.ps1        # FFmpeg 开发包自动下载
├── Makefile                    # MinGW g++ 构建
└── .gitignore
```

## FFmpeg 模块依赖

本项目链接以下 FFmpeg 库：

| 库 | 用途 |
|----|------|
| libavcodec | 编解码器（图像解码、音频编码、滤镜内部使用） |
| libavformat | 容器格式读写、流探测、格式自动检测 |
| libavutil | 内存管理、像素格式、帧操作、数学工具 |
| libswscale | 像素格式转换（任意格式 → RGBA） |
| libswresample | 音频采样格式转换（S16 ↔ FLTP） |
| libavfilter | 滤镜图（PSNR / SSIM / VMAF） |

## 依赖与许可

| 组件 | 用途 | 许可 |
|------|------|------|
| FFmpeg | 音视频编解码、容器、滤镜、像素转换 | LGPL / GPL |
| nanosvg | SVG 解析与光栅化 | zlib |

本项目为 GPL 许可。`libs/nanosvg/` 是源码级依赖（仅两个头文件），直接编译进程序。FFmpeg 通过脚本下载预编译包，不纳入仓库。
