// MediaGo — FFmpeg 原生转码引擎
// 使用 FFmpeg 标准管线：avformat → avcodec → avfilter → avcodec → avformat
// 不做自主推断，用户参数即最终行为

#pragma once

#include "transcode_config.h"

// ============================================================
// 转码结果
// ============================================================
struct TranscodeResult {
    bool ok = false;
    const char* error = nullptr;
};

// ============================================================
// 引擎接口
// ============================================================

// 执行转码任务，返回结果
// 保证输出经过完整的 avformat_write_header / av_write_trailer 封装
TranscodeResult transcode_run(const TranscodeConfig& cfg);
