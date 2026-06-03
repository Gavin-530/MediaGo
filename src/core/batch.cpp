// MediaGo - 批量处理引擎实现
// JSON 清单解析 → 参数合并 → 逐任务执行 → 结果汇总

#include "batch.h"
#include "../../libs/nlohmann/json.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir(path) _mkdir(path)
#define PATH_SEP '\\'
#else
#include <sys/stat.h>
#include <dirent.h>
#define mkdir(path) mkdir(path, 0755)
#define PATH_SEP '/'
#endif

using json = nlohmann::json;

// ============================================================
// 内部辅助：字符串操作
// ============================================================

static std::string dirname_of(const std::string& path) {
    auto pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

static std::string basename_of(const std::string& path) {
    auto pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

static std::string stem_of(const std::string& path) {
    std::string base = basename_of(path);
    auto pos = base.rfind('.');
    if (pos == std::string::npos) return base;
    return base.substr(0, pos);
}

static std::string ext_of(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    std::string ext = path.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return ext;
}

static std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == '\\' || a.back() == '/')
        return a + b;
    return a + PATH_SEP + b;
}

// ============================================================
// 内部辅助：目录创建（递归）
// ============================================================

static bool mkdir_p(const std::string& path) {
    if (path.empty() || path == ".") return true;
    if (mkdir(path.c_str()) == 0) return true;

#ifdef _WIN32
    // 目录已存在也算成功
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return true;

    std::string parent = dirname_of(path);
    if (parent != path && parent != "." && !mkdir_p(parent))
        return false;
    if (mkdir(path.c_str()) == 0) return true;
    // 再次检查（可能被其他进程创建）
    attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    if (errno == ENOENT) {
        std::string parent = dirname_of(path);
        if (parent != path && parent != "." && !mkdir_p(parent))
            return false;
        return mkdir(path.c_str()) == 0;
    }
    return (errno == EEXIST);
#endif
}

// ============================================================
// 内部辅助：媒体类型判断
// ============================================================

static bool is_image_ext(const std::string& ext) {
    return ext == ".png"  || ext == ".jpg"  || ext == ".jpeg" ||
           ext == ".bmp"  || ext == ".webp" || ext == ".avif" ||
           ext == ".heif" || ext == ".heic" || ext == ".tiff" ||
           ext == ".tif"  || ext == ".gif"  || ext == ".svg"  ||
           ext == ".ico"  || ext == ".ppm"  || ext == ".pgm"  ||
           ext == ".pbm"  || ext == ".pnm";
}

static bool is_video_ext(const std::string& ext) {
    return ext == ".mp4"  || ext == ".mkv"  || ext == ".avi"  ||
           ext == ".mov"  || ext == ".webm" || ext == ".flv"  ||
           ext == ".wmv"  || ext == ".m4v"  || ext == ".mpg"  ||
           ext == ".mpeg" || ext == ".ts"   || ext == ".m2ts" ||
           ext == ".3gp"  || ext == ".ogv"  || ext == ".mxf";
}

static const char* media_type_dir(const std::string& ext) {
    if (is_video_ext(ext))  return "videos";
    if (is_image_ext(ext))  return "images";
    return "other";
}

// ============================================================
// 内部辅助：通配符展开（Windows）
// ============================================================

#ifdef _WIN32
static void expand_wildcard(const std::string& pattern,
                            std::vector<std::string>& out) {
    if (pattern.find('*') == std::string::npos &&
        pattern.find('?') == std::string::npos) {
        out.push_back(pattern);
        return;
    }

    std::string dir = dirname_of(pattern);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == '.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        out.push_back(path_join(dir, fd.cFileName));
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}
#else
static void expand_wildcard(const std::string& pattern,
                            std::vector<std::string>& out) {
    out.push_back(pattern);
}
#endif

// ============================================================
// JSON 解析辅助函数
// ============================================================

static Strategy parse_strategy(const std::string& s) {
    if (s == "copy")   return Strategy::COPY;
    if (s == "encode") return Strategy::ENCODE;
    return Strategy::AUTO;
}

static ScaleMode parse_scale_mode(const std::string& s) {
    if (s == "fit")     return ScaleMode::FIT;
    if (s == "fill")    return ScaleMode::FILL;
    if (s == "stretch") return ScaleMode::STRETCH;
    return ScaleMode::NONE;
}

static ScaleAlgorithm parse_scale_algo(const std::string& s) {
    if (s == "fast_bilinear") return ScaleAlgorithm::FAST_BILINEAR;
    if (s == "bilinear")      return ScaleAlgorithm::BILINEAR;
    if (s == "bicubic")       return ScaleAlgorithm::BICUBIC;
    if (s == "box")           return ScaleAlgorithm::BOX;
    if (s == "gauss")         return ScaleAlgorithm::GAUSS;
    if (s == "sinc")          return ScaleAlgorithm::SINC;
    if (s == "lanczos")       return ScaleAlgorithm::LANCZOS;
    if (s == "spline")        return ScaleAlgorithm::SPLINE;
    if (s == "area")          return ScaleAlgorithm::AREA;
    return ScaleAlgorithm::LANCZOS;
}

static OutputStructure parse_output_structure(const std::string& s) {
    if (s == "flat")    return OutputStructure::FLAT;
    if (s == "by_type") return OutputStructure::BY_TYPE;
    return OutputStructure::BY_TYPE;
}

static const char* output_structure_str(OutputStructure os) {
    switch (os) {
        case OutputStructure::FLAT:    return "flat";
        case OutputStructure::BY_TYPE: return "by_type";
        default:                       return "by_type";
    }
}

// ============================================================
// 参数合并：base ImageConfig + json override → merged ImageConfig
//
// 重要：返回的 ImageConfig 中的 const char* 字段指向 backing 中的 std::string，
//       调用者必须确保 backing 在 ImageConfig 使用期间存活。
// ============================================================

static ImageConfig merge_params(const ImageConfig& base,
                                 const std::string& params_json,
                                 std::vector<std::string>& backing) {
    if (params_json.empty()) return base;

    ImageConfig result = base;
    try {
        json override = json::parse(params_json);

        if (override.contains("strategy") && override["strategy"].is_string())
            result.strategy = parse_strategy(override["strategy"].get<std::string>());

        // scale 子对象
        if (override.contains("scale") && override["scale"].is_object()) {
            const auto& s = override["scale"];
            if (s.contains("mode") && s["mode"].is_string())
                result.scale_mode = parse_scale_mode(s["mode"].get<std::string>());
            if (s.contains("width") && s["width"].is_number_integer())
                result.scale_w = s["width"].get<int>();
            if (s.contains("height") && s["height"].is_number_integer())
                result.scale_h = s["height"].get<int>();
            if (s.contains("algorithm") && s["algorithm"].is_string())
                result.scale_algorithm = parse_scale_algo(s["algorithm"].get<std::string>());
        }

        // 辅助：将 JSON 字符串存入 backing 并设置指针
        auto store_str = [&backing](const json& obj, const char* key,
                                     const char*& target) {
            if (obj.contains(key) && obj[key].is_string()) {
                backing.push_back(obj[key].get<std::string>());
                target = backing.back().c_str();
            }
        };

        // 扁平化顶层编码参数
        store_str(override, "codec", result.encode.name);
        store_str(override, "pix_fmt", result.encode.pixel_fmt);
        if (override.contains("quality") && override["quality"].is_number_integer())
            result.encode.quality = override["quality"].get<int>();
        if (override.contains("bitrate") && override["bitrate"].is_number_integer())
            result.encode.bitrate = override["bitrate"].get<int>();

        // encode 嵌套对象
        if (override.contains("encode") && override["encode"].is_object()) {
            const auto& e = override["encode"];
            store_str(e, "codec", result.encode.name);
            store_str(e, "pix_fmt", result.encode.pixel_fmt);
            if (e.contains("quality") && e["quality"].is_number_integer())
                result.encode.quality = e["quality"].get<int>();
            if (e.contains("bitrate") && e["bitrate"].is_number_integer())
                result.encode.bitrate = e["bitrate"].get<int>();
        }

        if (override.contains("preserve_icc") && override["preserve_icc"].is_boolean())
            result.preserve_icc = override["preserve_icc"].get<bool>();
        if (override.contains("preserve_metadata") && override["preserve_metadata"].is_boolean())
            result.preserve_metadata = override["preserve_metadata"].get<bool>();

    } catch (const json::exception&) {
        // 解析失败，保持 base 不变
    }
    return result;
}

// ============================================================
// 解析 ImageConfig from JSON（带生命周期管理）
// ============================================================

static ImageConfig parse_image_config_safe(const json& j,
                                            std::vector<std::string>& storage) {
    ImageConfig ic;

    if (j.is_null()) return ic;

    if (j.contains("strategy") && j["strategy"].is_string())
        ic.strategy = parse_strategy(j["strategy"].get<std::string>());

    if (j.contains("scale") && j["scale"].is_object()) {
        const auto& s = j["scale"];
        if (s.contains("mode") && s["mode"].is_string())
            ic.scale_mode = parse_scale_mode(s["mode"].get<std::string>());
        if (s.contains("width") && s["width"].is_number_integer())
            ic.scale_w = s["width"].get<int>();
        if (s.contains("height") && s["height"].is_number_integer())
            ic.scale_h = s["height"].get<int>();
        if (s.contains("algorithm") && s["algorithm"].is_string())
            ic.scale_algorithm = parse_scale_algo(s["algorithm"].get<std::string>());
    }

    auto try_store = [&storage](const json& obj, const char* key,
                                 const char*& target) {
        if (obj.contains(key) && obj[key].is_string()) {
            storage.push_back(obj[key].get<std::string>());
            target = storage.back().c_str();
        }
    };

    try_store(j, "codec", ic.encode.name);
    try_store(j, "pix_fmt", ic.encode.pixel_fmt);
    if (j.contains("quality")  && j["quality"].is_number_integer())  ic.encode.quality  = j["quality"].get<int>();
    if (j.contains("bitrate")  && j["bitrate"].is_number_integer())  ic.encode.bitrate  = j["bitrate"].get<int>();
    if (j.contains("gop_size") && j["gop_size"].is_number_integer()) ic.encode.gop_size  = j["gop_size"].get<int>();
    if (j.contains("threads")  && j["threads"].is_number_integer())  ic.encode.thread_count = j["threads"].get<int>();

    if (j.contains("encode") && j["encode"].is_object()) {
        const auto& e = j["encode"];
        try_store(e, "codec", ic.encode.name);
        try_store(e, "pix_fmt", ic.encode.pixel_fmt);
        if (e.contains("quality")  && e["quality"].is_number_integer())  ic.encode.quality  = e["quality"].get<int>();
        if (e.contains("bitrate")  && e["bitrate"].is_number_integer())  ic.encode.bitrate  = e["bitrate"].get<int>();
        if (e.contains("gop_size") && e["gop_size"].is_number_integer()) ic.encode.gop_size  = e["gop_size"].get<int>();
    }

    if (j.contains("preserve_icc")      && j["preserve_icc"].is_boolean())      ic.preserve_icc      = j["preserve_icc"].get<bool>();
    if (j.contains("preserve_metadata") && j["preserve_metadata"].is_boolean()) ic.preserve_metadata = j["preserve_metadata"].get<bool>();

    return ic;
}

// ============================================================
// JobManifest: from_json
// ============================================================

bool JobManifest::from_json(const char* path) {
    if (!path) return false;

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string content((size_t)size, '\0');
    if (size > 0) fread(&content[0], 1, (size_t)size, f);
    fclose(f);

    try {
        json root = json::parse(content);

        if (root.contains("version") && root["version"].is_number_integer())
            version = root["version"].get<int>();

        // output
        if (root.contains("output") && root["output"].is_object()) {
            const auto& out = root["output"];
            if (out.contains("dir") && out["dir"].is_string())
                config.output_dir = out["dir"].get<std::string>();
            if (out.contains("structure") && out["structure"].is_string())
                config.output_structure = parse_output_structure(
                    out["structure"].get<std::string>());
        }

        // defaults — 字符串存入 m_defaults_storage 防止悬空
        if (root.contains("defaults") && root["defaults"].is_object()) {
            m_defaults_storage.clear();
            config.defaults = parse_image_config_safe(root["defaults"],
                                                       m_defaults_storage);
        }

        // jobs
        if (root.contains("jobs") && root["jobs"].is_array()) {
            for (const auto& j : root["jobs"]) {
                BatchJob job;
                if (j.contains("input") && j["input"].is_string())
                    job.input = j["input"].get<std::string>();
                else
                    continue;

                if (j.contains("output") && j["output"].is_string())
                    job.output = j["output"].get<std::string>();

                if (j.contains("params") && j["params"].is_object())
                    job.params_json = j["params"].dump();

                jobs.push_back(std::move(job));
            }
        }

        return true;
    } catch (const json::exception&) {
        return false;
    }
}

// ============================================================
// JobManifest: to_json
// ============================================================

bool JobManifest::to_json(const char* path) const {
    if (!path) return false;

    try {
        json root;
        root["version"] = version;

        json out;
        out["dir"]       = config.output_dir;
        out["structure"] = output_structure_str(config.output_structure);
        root["output"]   = out;

        json defs;
        defs["strategy"] = "auto";
        if (config.defaults.encode.quality >= 0)
            defs["quality"] = config.defaults.encode.quality;
        root["defaults"] = defs;

        json ja = json::array();
        for (const auto& job : jobs) {
            json j;
            j["input"]  = job.input;
            if (!job.output.empty())
                j["output"] = job.output;
            if (!job.params_json.empty()) {
                try {
                    j["params"] = json::parse(job.params_json);
                } catch (...) {}
            }
            ja.push_back(j);
        }
        root["jobs"] = ja;

        std::string s = root.dump(4);
        FILE* f = fopen(path, "wb");
        if (!f) return false;
        fwrite(s.c_str(), 1, s.size(), f);
        fclose(f);
        return true;
    } catch (const json::exception&) {
        return false;
    }
}

// ============================================================
// make_output_path：自动生成输出路径
// ============================================================

std::string BatchProcessor::make_output_path(const std::string& input,
                                              const BatchConfig& config,
                                              const std::string& ext) {
    std::string stem = stem_of(input);
    std::string out_ext = ext.empty() ? ext_of(input) : ext;
    if (out_ext.empty()) out_ext = ".mp4";

    std::string rel;
    if (config.output_structure == OutputStructure::BY_TYPE) {
        std::string type = media_type_dir(out_ext);
        rel = std::string(type) + PATH_SEP + stem + out_ext;
    } else {
        rel = stem + out_ext;
    }

    return path_join(config.output_dir, rel);
}

// ============================================================
// BatchProcessor 成员函数
// ============================================================

void BatchProcessor::set_progress_callback(ProgressCallback fn, void* user) {
    m_progress = fn;
    m_progress_user = user;
}

const std::vector<JobResult>& BatchProcessor::results() const {
    return m_results;
}

int BatchProcessor::success_count() const {
    int n = 0;
    for (const auto& r : m_results)
        if (r.ok) n++;
    return n;
}

int BatchProcessor::fail_count() const {
    return (int)m_results.size() - success_count();
}

static void notify_progress(BatchProcessor::ProgressCallback fn, void* user,
                            int current, int total,
                            const std::string& file, const char* status) {
    if (fn)
        fn(current, total, file.c_str(), status, user);
}

bool BatchProcessor::process_one(const BatchJob& job,
                                  const BatchConfig& config, int index) {
    // 展开通配符
    std::vector<std::string> input_files;
    expand_wildcard(job.input, input_files);

    if (input_files.empty()) {
        JobResult result;
        result.input  = job.input;
        result.ok     = false;
        result.error  = "文件不存在或无匹配: " + job.input;
        m_results.push_back(std::move(result));
        return false;
    }

    bool any_ok = false;

    for (size_t fi = 0; fi < input_files.size(); fi++) {
        const std::string& input_path = input_files[fi];

        JobResult result;
        result.input = input_path;

        notify_progress(m_progress, m_progress_user,
                        index, 0, basename_of(input_path), "processing");

        // 确定输出路径
        std::string output_path;
        if (!job.output.empty()) {
            output_path = path_join(config.output_dir, job.output);
            // 多文件通配时，输出路径不能重复；加序号
            if (input_files.size() > 1 && fi > 0) {
                std::string stem = stem_of(output_path);
                std::string ext  = ext_of(output_path);
                output_path = stem + "_" + std::to_string(fi) + ext;
                // 重新拼接路径
                std::string dir = dirname_of(output_path);
                output_path = path_join(dir, stem_of(output_path) + ext);
            }
        } else {
            output_path = make_output_path(input_path, config);
        }
        result.output = output_path;

        // 确保输出目录存在
        std::string out_dir = dirname_of(output_path);
        if (!mkdir_p(out_dir)) {
            result.ok    = false;
            result.error = "无法创建输出目录: " + out_dir;
            notify_progress(m_progress, m_progress_user,
                           index, 0, basename_of(input_path), "fail");
            m_results.push_back(std::move(result));
            continue;
        }

        // 合并参数：global defaults + per-job overrides
        // backing 确保 const char* 字符串在 process_media 调用期间存活
        std::vector<std::string> backing;
        ImageConfig effective = merge_params(config.defaults, job.params_json, backing);

        // 执行处理
        ProcessReport report;
        memset(&report, 0, sizeof(report));
        TranscodeResult tr = process_media(input_path.c_str(),
                                            output_path.c_str(),
                                            effective, &report);

        if (tr.ok) {
            result.ok     = true;
            result.report = report;
            any_ok = true;
            notify_progress(m_progress, m_progress_user,
                           index, 0, basename_of(input_path), "ok");
        } else {
            result.ok    = false;
            result.error = tr.error ? tr.error : "unknown error";
            notify_progress(m_progress, m_progress_user,
                           index, 0, basename_of(input_path), "fail");
        }

        m_results.push_back(std::move(result));
    }

    return any_ok;
}

bool BatchProcessor::process(const JobManifest& manifest) {
    m_results.clear();
    m_results.reserve(manifest.jobs.size());

    int total = (int)manifest.jobs.size();

    for (int i = 0; i < total; i++) {
        process_one(manifest.jobs[i], manifest.config, i);
    }

    // 打印汇总
    printf("\n========== 批量处理汇总 ==========\n");
    printf("  总任务: %d\n", total);
    printf("  成功:   %d\n", success_count());
    printf("  失败:   %d\n", fail_count());

    if (fail_count() > 0) {
        printf("\n失败列表:\n");
        for (const auto& r : m_results) {
            if (!r.ok) {
                printf("  [FAIL] %s\n", r.input.c_str());
                printf("         -> %s\n", r.error.c_str());
            }
        }
    }

    printf("==================================\n");
    return true;
}

bool BatchProcessor::process(const char* manifest_path) {
    JobManifest manifest;
    if (!manifest.from_json(manifest_path)) {
        fprintf(stderr, "错误: 无法解析清单文件: %s\n", manifest_path);
        return false;
    }

    printf("=== 批量处理清单 ===\n");
    printf("  清单文件 : %s\n", manifest_path);
    printf("  输出目录 : %s\n", manifest.config.output_dir.c_str());
    printf("  目录结构 : %s\n",
           manifest.config.output_structure == OutputStructure::FLAT
               ? "flat" : "by_type");
    printf("  任务数量 : %zu\n\n", manifest.jobs.size());

    return process(manifest);
}
