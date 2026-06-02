// MediaGo - 统一媒体 I/O 模块
// 光栅图通过 FFmpeg codec 处理（PNG/JPEG/BMP/WebP/AVIF/HEIF 等 30+ 格式）
// 矢量图通过 nanosvg 处理（SVG 解析 + 光栅化）
// 所有内存统一走 av_malloc/av_free，杜绝分配器混用

#pragma once
#include <cstdint>

// ---- 光栅图（FFmpeg codec）----

// 读取图像文件，解码为 RGBA 像素数据
// 成功返回 data 指针（用 media_free 释放），失败返回 nullptr
uint8_t* media_load(const char* path, int* w, int* h);

// 保存 RGBA 像素数据为 PNG
bool media_save_png(const char* path, int w, int h, const uint8_t* data);

// 保存 RGBA 像素数据为 JPEG (quality 1-100)
bool media_save_jpg(const char* path, int w, int h, const uint8_t* data, int quality);

// 释放 media_load / svg_rasterize 返回的内存
void media_free(uint8_t* data);

// ---- 矢量图 (nanosvg) ----

// 解析 SVG 文件并光栅化为 RGBA 像素
// 成功返回 data 指针（用 media_free 释放），失败返回 nullptr
uint8_t* svg_rasterize(const char* path, int w, int h);
