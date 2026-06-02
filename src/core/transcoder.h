// MediaGo - 转码引擎
// remux（容器转换）与图片格式转换

#pragma once

struct TranscodeResult {
    bool ok;
    const char* error;  // 失败时的静态描述，无需释放
};

// 媒体文件转封装/转码（自动匹配输入流，复制到输出容器）
// 根据输出扩展名选择容器格式，支持 MP4/MKV/WebM/AVI/FLV 等
TranscodeResult transcode_media(const char* input, const char* output);

// 图片格式转换（PNG ↔ JPEG，通过 media_load + media_save）
// 根据输出扩展名选择格式
TranscodeResult convert_image(const char* input, const char* output);
