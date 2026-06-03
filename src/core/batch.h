// MediaGo - 批量处理引擎
// 统一管理多文件输入，支持全局默认参数 + 单文件独立覆盖
// 设计面向 CLI 批量操作与前端 GUI 可视化对接
//
// 核心概念：
//   JobManifest   — JSON 清单文件，描述一批处理任务
//   BatchJob      — 单个任务：输入路径 + 可选输出路径 + 参数覆盖
//   BatchConfig   — 批量配置：输出目录、目录结构、全局默认参数
//   BatchProcessor — 执行引擎：按序处理所有任务，支持进度回调
//
// JSON 清单格式示例：
// {
//   "version": 1,
//   "output": { "dir": "./output", "structure": "by_type" },
//   "defaults": {
//     "strategy": "auto",
//     "codec": "libx264",
//     "quality": 80
//   },
//   "jobs": [
//     {
//       "input": "videos/demo.mp4",
//       "output": "demo_h265.mp4",
//       "params": { "codec": "libx265", "quality": 28 }
//     },
//     {
//       "input": "images/photo.jpg",
//       "params": { "scale": { "width": 640, "height": 480 } }
//     }
//   ]
// }

#pragma once

#include "config.h"
#include "transcoder.h"
#include <string>
#include <vector>

// ---- 输出目录组织模式 ----
enum class OutputStructure {
    FLAT,      // 所有文件直接放在 output_dir 下
    BY_TYPE,   // output_dir/images/ 或 output_dir/videos/
};

// ---- 批量配置 ----
struct BatchConfig {
    std::string output_dir = "./output";
    OutputStructure output_structure = OutputStructure::BY_TYPE;
    ImageConfig defaults;    // 全局默认处理参数
};

// ---- 单个任务描述 ----
struct BatchJob {
    std::string input;          // 输入文件路径（支持通配符 * 和 ?）
    std::string output;         // 输出路径（为空则自动生成）
    std::string params_json;    // 单文件参数覆盖（JSON 字符串，可选）
};

// ---- 单任务执行结果 ----
struct JobResult {
    std::string input;
    std::string output;
    bool ok = false;
    std::string error;           // 失败时的错误描述
    ProcessReport report;        // 成功时的处理报告
};

// ---- JSON 清单 ----
class JobManifest {
public:
    int version = 1;
    BatchConfig config;
    std::vector<BatchJob> jobs;

    // 从 JSON 文件加载清单
    bool from_json(const char* path);

    // 导出到 JSON 文件（生成清单模板）
    bool to_json(const char* path) const;

private:
    // 持久存储 config.defaults 中 const char* 字段引用的字符串
    // 防止 JSON 解析后字符串悬空
    std::vector<std::string> m_defaults_storage;
};

// ---- 批量处理器 ----
class BatchProcessor {
public:
    // 进度回调类型
    //   current: 当前任务序号（0-based）
    //   total:   总任务数
    //   file:    当前处理的文件名
    //   status:  "processing" / "ok" / "fail"
    //   user:    用户自定义指针
    using ProgressCallback = void(*)(int current, int total,
                                     const char* file, const char* status,
                                     void* user);

    // 处理清单中的所有任务
    // 返回 true 表示清单解析成功且所有任务已执行（部分失败不影响返回值）
    bool process(const JobManifest& manifest);
    bool process(const char* manifest_path);

    // 设置进度回调（供 GUI 使用）
    void set_progress_callback(ProgressCallback fn, void* user = nullptr);

    // 查询结果
    const std::vector<JobResult>& results() const;
    int success_count() const;
    int fail_count() const;

    // 自动生成输出路径
    // input:  输入文件路径
    // config: 批量配置
    // ext:    输出扩展名（为空则保持原扩展名）
    static std::string make_output_path(const std::string& input,
                                         const BatchConfig& config,
                                         const std::string& ext = "");

private:
    bool process_one(const BatchJob& job, const BatchConfig& config, int index);

    std::vector<JobResult> m_results;
    ProgressCallback m_progress = nullptr;
    void* m_progress_user = nullptr;
};
