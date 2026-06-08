// MediaGo — 批量处理引擎实现
// JSON 清单驱动，直接调用原生 FFmpeg 转码管线

#include "batch.h"
#include "transcode_engine.h"

#include "nlohmann/json.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <glob.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

// ============================================================
// 路径工具（避免 std::filesystem 在 GCC 8.x 的兼容性问题）
// ============================================================

// 获取路径的父目录
static std::string path_parent(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

// 获取文件名（不含目录）
static std::string path_filename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// 获取扩展名
static std::string path_extension(const std::string& path) {
    std::string name = path_filename(path);
    size_t pos = name.rfind('.');
    if (pos == std::string::npos) return "";
    return name.substr(pos);
}

// 获取文件名（不含扩展名）
static std::string path_stem(const std::string& path) {
    std::string name = path_filename(path);
    size_t pos = name.rfind('.');
    if (pos == std::string::npos) return name;
    return name.substr(0, pos);
}

// 创建目录（递归）
static bool make_dirs(const std::string& path) {
#ifdef _WIN32
    std::string dir = path;
    // 将路径转为可用的 mkdir 格式
    for (auto& c : dir) if (c == '/') c = '\\';
    // 递归创建
    std::string cur;
    for (char c : dir) {
        cur += c;
        if (c == '\\' || c == ':') {
            if (cur.size() > 1 && cur.back() != ':') {
                CreateDirectoryA(cur.c_str(), nullptr);
            }
        }
    }
    CreateDirectoryA(dir.c_str(), nullptr);
    return true;
#else
    // POSIX: 使用 mkdir() 或 system("mkdir -p")
    std::string cmd = "mkdir -p \"" + path + "\" 2>/dev/null";
    return system(cmd.c_str()) == 0;
#endif
}

// 文件是否存在
static bool file_exists(const std::string& path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    FILE* f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return true; }
    return false;
#endif
}


// ============================================================
// 文件类型判断
// ============================================================

static bool is_image_ext(const std::string& path) {
    std::string ext = path;
    size_t dot = ext.rfind('.');
    if (dot == std::string::npos) return false;
    ext = ext.substr(dot);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp"
        || ext == ".webp" || ext == ".avif" || ext == ".heif" || ext == ".heic"
        || ext == ".tiff" || ext == ".tif" || ext == ".svg"
        || ext == ".ico" || ext == ".gif";
}

static bool is_video_ext(const std::string& path) {
    std::string ext = path;
    size_t dot = ext.rfind('.');
    if (dot == std::string::npos) return false;
    ext = ext.substr(dot);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov"
        || ext == ".webm" || ext == ".flv" || ext == ".wmv" || ext == ".m4v"
        || ext == ".mpg" || ext == ".mpeg" || ext == ".ts" || ext == ".m2ts"
        || ext == ".3gp" || ext == ".ogv" || ext == ".mxf" || ext == ".h264"
        || ext == ".hevc" || ext == ".265" || ext == ".yuv" || ext == ".y4m";
}

static std::string file_category(const std::string& path) {
    if (is_image_ext(path)) return "images";
    if (is_video_ext(path)) return "videos";
    return "other";
}

// ============================================================
// 通配符展开
// ============================================================

std::vector<std::string> BatchProcessor::expand_wildcard(const std::string& pattern) {
    std::vector<std::string> result;

#ifdef _WIN32
    // Windows: 使用 FindFirstFile
    std::string dir = path_parent(pattern);
    std::string mask = path_filename(pattern);

    std::string search_path;
    if (dir.empty() || dir == ".") search_path = ".\\" + mask;
    else search_path = dir + "\\" + mask;

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(search_path.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        result.push_back(pattern); // 无匹配则原样返回
        return result;
    }

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') {
            if (dir.empty()) result.push_back(fd.cFileName);
            else result.push_back(dir + "\\" + fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    glob_t g;
    if (glob(pattern.c_str(), GLOB_NOSORT, nullptr, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; i++)
            result.push_back(g.gl_pathv[i]);
        globfree(&g);
    }
#endif

    if (result.empty())
        result.push_back(pattern);

    return result;
}

// ============================================================
// 输出路径生成
// ============================================================

std::string BatchProcessor::make_output_path(const std::string& input,
                                              const std::string& job_output) {
    std::string stem = path_stem(input);
    std::string ext = path_extension(input);

    if (!job_output.empty()) {
        // 简单判断绝对路径
        if (!job_output.empty() && (job_output[0] == '/' || job_output[0] == '\\' ||
            (job_output.size() > 1 && job_output[1] == ':')))
            return job_output;
        return output_cfg_.dir + "/" + job_output;
    }

    if (output_cfg_.structure == "flat") {
        return output_cfg_.dir + "/" + stem + ext;
    }

    // by_type
    std::string category = file_category(input);
    return output_cfg_.dir + "/" + category + "/" + stem + ext;
}

// ============================================================
// 参数合并
// ============================================================

TranscodeConfig BatchProcessor::merge_config(const BatchJobItem& job,
                                               const std::string& input_path,
                                               const std::string& output_path) {
    TranscodeConfig cfg;
    cfg.input = input_path.c_str();
    cfg.output = output_path.c_str();
    cfg.overwrite = job.overwrite;
    cfg.format = job.format.empty() ? nullptr : job.format.c_str();

    // 视频
    if (job.video_copy) {
        cfg.video.codec = "copy";
    } else if (!job.video_codec.empty()) {
        cfg.video.codec = job.video_codec.c_str();
    } else {
        cfg.video.codec = nullptr; // copy
    }
    cfg.video.crf = job.video_crf;
    cfg.video.bitrate = job.video_bitrate;
    cfg.video.maxrate = job.video_maxrate;
    cfg.video.bufsize = job.video_bufsize;
    cfg.video.width = job.video_width;
    cfg.video.height = job.video_height;
    cfg.video.fps = job.video_fps;
    cfg.video.keep_aspect = job.video_keep_aspect;
    cfg.video.preset = job.video_preset.empty() ? nullptr : job.video_preset.c_str();
    cfg.video.tune = job.video_tune.empty() ? nullptr : job.video_tune.c_str();
    cfg.video.profile = job.video_profile.empty() ? nullptr : job.video_profile.c_str();
    cfg.video.pixel_fmt = job.video_pixel_fmt.empty() ? nullptr : job.video_pixel_fmt.c_str();
    cfg.video.gop_size = job.video_gop_size;
    cfg.video.threads = job.video_threads;
    cfg.video.b_frames = job.video_b_frames;
    cfg.video.qmin = job.video_qmin;
    cfg.video.qmax = job.video_qmax;
    cfg.video.level = job.video_level.empty() ? nullptr : job.video_level.c_str();
    cfg.video.opts_json = job.video_opts_json.empty() ? nullptr : job.video_opts_json.c_str();

    // 音频
    if (job.audio_copy) {
        cfg.audio.codec = "copy";
    } else if (!job.audio_codec.empty()) {
        cfg.audio.codec = job.audio_codec.c_str();
    } else {
        cfg.audio.codec = nullptr;
    }
    cfg.audio.bitrate = job.audio_bitrate;
    cfg.audio.sample_rate = job.audio_sample_rate;
    cfg.audio.channel_layout = job.audio_channel_layout.empty() ? nullptr : job.audio_channel_layout.c_str();
    cfg.audio.compression_level = job.audio_compression_level;
    cfg.audio.opts_json = job.audio_opts_json.empty() ? nullptr : job.audio_opts_json.c_str();

    return cfg;
}

// ============================================================
// JSON 解析
// ============================================================

static BatchJobItem parse_job_item(const json& j) {
    BatchJobItem item;
    item.input = j.value("input", "");

    if (j.contains("output"))
        item.output = j["output"].get<std::string>();

    if (j.contains("video")) {
        auto& v = j["video"];
        if (v.contains("copy") && v.value("copy", false)) {
            item.video_copy = true;
        } else if (v.contains("codec")) {
            item.video_codec = v.value("codec", "");
        }
        item.video_crf     = v.value("crf", -1);
        item.video_bitrate = v.value("bitrate", (int64_t)0);
        item.video_maxrate = v.value("maxrate", (int64_t)0);
        item.video_bufsize = v.value("bufsize", (int64_t)0);
        item.video_width   = v.value("width", 0);
        item.video_height  = v.value("height", 0);
        item.video_fps     = v.value("fps", 0.0);
        item.video_keep_aspect = v.value("keep_aspect", true);
        item.video_preset  = v.value("preset", "");
        item.video_tune    = v.value("tune", "");
        item.video_profile = v.value("profile", "");
        item.video_pixel_fmt = v.value("pixel_fmt", "");
        item.video_gop_size  = v.value("gop_size", 0);
        item.video_threads   = v.value("threads", 0);
        item.video_b_frames  = v.value("b_frames", -1);
        item.video_qmin      = v.value("qmin", -1);
        item.video_qmax      = v.value("qmax", -1);
        item.video_level     = v.value("level", "");
        if (v.contains("opts") && v["opts"].is_object()) {
            item.video_opts_json = v["opts"].dump();
        }
    }

    if (j.contains("audio")) {
        auto& a = j["audio"];
        if (a.contains("copy") && a.value("copy", false)) {
            item.audio_copy = true;
        } else if (a.contains("codec")) {
            item.audio_codec = a.value("codec", "");
        }
        item.audio_bitrate = a.value("bitrate", (int64_t)0);
        item.audio_sample_rate = a.value("sample_rate", 0);
        item.audio_channel_layout = a.value("channel_layout", "");
        item.audio_compression_level = a.value("compression_level", -1);
        if (a.contains("opts") && a["opts"].is_object()) {
            item.audio_opts_json = a["opts"].dump();
        }
    }

    if (j.contains("format"))
        item.format = j.value("format", "");
    if (j.contains("overwrite"))
        item.overwrite = j.value("overwrite", false);

    return item;
}

bool BatchProcessor::parse_manifest(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        error_ = std::string("cannot open manifest: ") + path;
        return false;
    }

    json j;
    try { j = json::parse(f); }
    catch (const std::exception& e) {
        error_ = std::string("JSON parse error: ") + e.what();
        return false;
    }

    // output
    if (j.contains("output")) {
        auto& o = j["output"];
        output_cfg_.dir = o.value("dir", "./output");
        output_cfg_.structure = o.value("structure", "by_type");
    }

    // defaults
    if (j.contains("defaults")) {
        json d = j["defaults"];
        // 合并 defaults 下的 video/audio/format/overwrite 等
        defaults_ = parse_job_item(d);
    }

    // jobs
    if (!j.contains("jobs") || !j["jobs"].is_array()) {
        error_ = "missing 'jobs' array in manifest";
        return false;
    }

    for (auto& job_json : j["jobs"]) {
        jobs_.push_back(parse_job_item(job_json));
    }

    if (jobs_.empty()) {
        error_ = "no jobs in manifest";
        return false;
    }

    return true;
}

// ============================================================
// 合并 defaults
// ============================================================

static BatchJobItem merge_job(const BatchJobItem& def, const BatchJobItem& job) {
    BatchJobItem m = def; // 以 defaults 为底

    if (!job.input.empty())          m.input = job.input;
    if (!job.output.empty())         m.output = job.output;
    if (!job.video_codec.empty())    m.video_codec = job.video_codec;
    if (job.video_copy)              { m.video_copy = true; m.video_codec.clear(); }
    if (job.video_crf >= 0)          m.video_crf = job.video_crf;
    if (job.video_bitrate > 0)       m.video_bitrate = job.video_bitrate;
    if (job.video_width > 0)         m.video_width = job.video_width;
    if (job.video_height > 0)        m.video_height = job.video_height;
    if (job.video_fps > 0)           m.video_fps = job.video_fps;
    if (!job.video_preset.empty())   m.video_preset = job.video_preset;
    if (!job.video_tune.empty())     m.video_tune = job.video_tune;
    m.video_keep_aspect = job.video_keep_aspect;
    if (!job.video_opts_json.empty()) m.video_opts_json = job.video_opts_json;

    if (!job.audio_codec.empty())    m.audio_codec = job.audio_codec;
    if (job.audio_copy)              { m.audio_copy = true; m.audio_codec.clear(); }
    if (job.audio_bitrate > 0)       m.audio_bitrate = job.audio_bitrate;
    if (!job.audio_opts_json.empty()) m.audio_opts_json = job.audio_opts_json;

    if (!job.format.empty())         m.format = job.format;
    if (job.overwrite)               m.overwrite = job.overwrite;

    return m;
}

// ============================================================
// 主流程
// ============================================================

bool BatchProcessor::process(const char* manifest_path,
                               ProgressCallback on_progress) {
    if (!parse_manifest(manifest_path)) {
        fprintf(stderr, "[MediaGo] manifest error: %s\n", error_.c_str());
        return false;
    }

    // 确保输出目录存在
    make_dirs(output_cfg_.dir);

    process_all(on_progress);

    // 汇总
    fprintf(stderr, "\n========== 批量处理汇总 ==========\n");
    unsigned ok_count = 0;
    for (auto& r : results_) {
        if (r.ok) ok_count++;
    }
    fprintf(stderr, "  总任务: %zu\n", results_.size());
    fprintf(stderr, "  成功:   %u\n", ok_count);
    fprintf(stderr, "  失败:   %zu\n\n", results_.size() - ok_count);

    if (ok_count < results_.size()) {
        fprintf(stderr, "失败列表:\n");
        for (auto& r : results_) {
            if (!r.ok) {
                fprintf(stderr, "  [FAIL] %s\n", r.input.c_str());
                fprintf(stderr, "         -> %s\n", r.error.c_str());
            }
        }
    }
    fprintf(stderr, "==================================\n");

    return ok_count == results_.size();
}

void BatchProcessor::process_all(ProgressCallback on_progress) {
    results_.clear();

    for (size_t i = 0; i < jobs_.size(); i++) {
        // 合并 defaults
        BatchJobItem job = merge_job(defaults_, jobs_[i]);

        // 展开通配符
        auto files = expand_wildcard(job.input);
        if (files.empty()) {
            BatchJobResult r;
            r.ok = false;
            r.input = job.input;
            r.error = "no files matched";
            results_.push_back(r);
            if (on_progress) on_progress((unsigned)i, (unsigned)jobs_.size(), JobStatus::Fail, job.input, "");
            continue;
        }

        // 多文件 + 指定输出 → 自动加序号
        bool need_suffix = (files.size() > 1 && !job.output.empty());

        for (size_t f = 0; f < files.size(); f++) {
            std::string out_path;
            if (need_suffix) {
                std::string stem = path_stem(job.output);
                std::string ext = path_extension(job.output);
                out_path = output_cfg_.dir + "/" + stem + "_" + std::to_string(f + 1) + ext;
            } else {
                out_path = make_output_path(files[f], job.output);
            }

            TranscodeConfig cfg = merge_config(job, files[f], out_path);

            // 输入文件不存在
            if (!file_exists(files[f])) {
                BatchJobResult r;
                r.ok = false;
                r.input = files[f];
                r.output = out_path;
                r.error = "file not found: " + files[f];
                results_.push_back(r);
                if (on_progress) on_progress((unsigned)i, (unsigned)jobs_.size(), JobStatus::Fail, files[f], out_path);
                continue;
            }

            // 输出目录
            std::string out_parent = path_parent(out_path);
            if (!out_parent.empty() && out_parent != ".") {
                make_dirs(out_parent);
            }

            // 覆盖检查
            if (!cfg.overwrite && file_exists(out_path)) {
                BatchJobResult r;
                r.ok = false;
                r.input = files[f];
                r.output = out_path;
                r.error = "output exists (use overwrite:true to force)";
                results_.push_back(r);
                if (on_progress) on_progress((unsigned)i, (unsigned)jobs_.size(), JobStatus::Fail, files[f], out_path);
                continue;
            }

            if (on_progress) on_progress((unsigned)i, (unsigned)jobs_.size(), JobStatus::Processing, files[f], out_path);

            BatchJobResult result;
            result.input = files[f];
            result.output = out_path;

            bool ok = run_one(cfg, &result);
            results_.push_back(result);

            if (on_progress) {
                on_progress((unsigned)i, (unsigned)jobs_.size(),
                             ok ? JobStatus::OK : JobStatus::Fail, files[f], out_path);
            }
        }
    }
}

// ============================================================
// 单个转码
// ============================================================

bool BatchProcessor::run_one(const TranscodeConfig& cfg, BatchJobResult* result) {
    auto t0 = std::chrono::steady_clock::now();

    result->use_copy_video = (cfg.video.codec == nullptr || !strcmp(cfg.video.codec, "copy"));
    result->use_copy_audio = (cfg.audio.codec == nullptr || !strcmp(cfg.audio.codec, "copy"));

    TranscodeResult tr = transcode_run(cfg);

    auto t1 = std::chrono::steady_clock::now();
    result->elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    if (!tr.ok) {
        result->ok = false;
        result->error = tr.error ? tr.error : "unknown error";
        return false;
    }

    result->ok = true;
    return true;
}
