// MediaGo — 批量处理引擎 v2
// JSON 清单驱动的批量转码，支持视频/音频/图像混合处理

#pragma once

#include "transcode_config.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ============================================================
// 结果
// ============================================================
struct BatchJobResult {
    bool ok = false;
    std::string input;
    std::string output;
    std::string error;
    bool use_copy_video = false;
    bool use_copy_audio = false;
    int64_t elapsed_ms = 0;
};

// ============================================================
// 进度回调
// ============================================================
enum class JobStatus { Processing, OK, Fail };

using ProgressCallback = std::function<void(unsigned index, unsigned total,
                                             JobStatus status,
                                             const std::string& input)>;

// ============================================================
// 清单结构（内部解析用）
// ============================================================
struct BatchOutputCfg {
    std::string dir = "./output";
    std::string structure = "by_type"; // "flat" | "by_type"
};

struct BatchJobItem {
    std::string input;            // 必填，支持通配符
    std::string output;           // 可选
    std::string video_codec;      // null/"copy"=拷贝
    int video_crf = -1;
    int64_t video_bitrate = 0;
    int video_width = 0;
    int video_height = 0;
    double video_fps = 0.0;
    bool video_keep_aspect = true;
    bool video_copy = false;
    std::string video_preset;
    std::string video_tune;

    std::string audio_codec;     // null/"copy"=拷贝
    int64_t audio_bitrate = 0;
    bool audio_copy = false;

    std::string format;          // 容器格式
    bool overwrite = false;
};

// ============================================================
// 处理器
// ============================================================
class BatchProcessor {
public:
    // 加载并执行 JSON 清单
    bool process(const char* manifest_path,
                 ProgressCallback on_progress = nullptr);

private:
    // JSON 解析
    bool parse_manifest(const char* path);
    // 展开任务并执行
    void process_all(ProgressCallback on_progress);

    // 通配符展开
    std::vector<std::string> expand_wildcard(const std::string& pattern);
    // 生成输出路径
    std::string make_output_path(const std::string& input,
                                  const std::string& job_output);
    // 合并参数
    TranscodeConfig merge_config(const BatchJobItem& job,
                                  const std::string& input_path,
                                  const std::string& output_path);

    // 执行单个转码
    bool run_one(const TranscodeConfig& cfg, BatchJobResult* result);

    BatchOutputCfg output_cfg_;
    BatchJobItem defaults_;
    std::vector<BatchJobItem> jobs_;
    std::string error_;

    // 结果
    std::vector<BatchJobResult> results_;
};
