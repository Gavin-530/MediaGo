# MediaGo

跨平台媒体处理工具，基于 FFmpeg 的完整音视频编解码解决方案。

## 功能

- 软/硬件编解码（x264/x265、AV1、NVENC、AMF、VAAPI、Vulkan、D3D12VA）
- 多媒体容器格式支持（MP4、MKV、WebM、AVI、FLV）
- Netflix VMAF 视频质量评估
- 跨平台架构（核心层平台无关，平台胶水层隔离差异）

## 环境依赖

- **MinGW-w64** (g++ 8.1+)
- **FFmpeg** (BtbN gpl-shared 预编译包)

## 快速开始

```powershell
# 1. 下载 FFmpeg 完整开发包（含 VMAF、x264/x265、硬件编解码器）
powershell -ExecutionPolicy Bypass -File scripts/setup_ffmpeg.ps1

# 2. 编译主程序
mingw32-make

# 3. 运行
mingw32-make run
```

## 构建目标

| 命令 | 产物 | 说明 |
|------|------|------|
| `mingw32-make` | `build/MediaGo.exe` | 主程序 |
| `mingw32-make diag` | `build/diag.exe` | 环境诊断工具 |
| `mingw32-make clean` | - | 清理构建产物 |

## 项目结构

```
MediaGo/
├── src/
│   ├── main.cpp            # App 入口
│   ├── diag_main.cpp       # 诊断工具入口
│   └── core/               # 可复用模块
│       ├── diag.h / .cpp   # FFmpeg 诊断
│       └── encoder.h / .cpp # 编码封装
├── scripts/
│   └── setup_ffmpeg.ps1    # FFmpeg 环境配置
├── Makefile                # 构建配置
└── .gitignore
```

## 许可

本项目遵循 GPL 许可。依赖组件：
- FFmpeg (LGPL/GPL)
- x264 / x265 (GPL)
- libvmaf (BSD)
