// MediaGo — HTTP 服务层实现
// REST API + SSE 进度推送 + 静态文件服务

#include "http_server.h"
#include "core/batch.h"
#include "core/config.h"
#include "core/media_io.h"
#include "core/transcode_engine.h"

#include "nlohmann/json.hpp"
#include "cpp-httplib/httplib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

using json = nlohmann::json;

// ============================================================
// 内部实现
// ============================================================

struct MediaGoServer::Impl {
    httplib::Server svr;
    std::string data_dir = "./data";
    std::string upload_dir;     // data/uploads
    std::string manifest_dir;   // data/manifests
    std::string history_path;   // data/history.json

    // ---- 任务管理 ----
    struct TaskState {
        TaskProgress progress;
        std::mutex mtx;
        std::condition_variable cv;
        bool done = false;
        int update_seq = 0;       // 进度更新序号（供 SSE 去重）
    };
    std::map<std::string, std::shared_ptr<TaskState>> tasks;
    std::mutex tasks_mtx;

    // 生成唯一任务 ID
    static std::string make_task_id() {
        static std::mutex mtx;
        static std::mt19937 rng((unsigned)std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count());
        std::lock_guard<std::mutex> lock(mtx);
        std::uniform_int_distribution<uint64_t> dist;
        std::ostringstream oss;
        oss << std::hex << dist(rng) << dist(rng);
        return oss.str();
    }
};

// ============================================================
// 构造 / 析构
// ============================================================

MediaGoServer::MediaGoServer() : impl_(std::make_unique<Impl>()) {}

MediaGoServer::~MediaGoServer() { stop(); }

// ============================================================
// 路由注册
// ============================================================

void MediaGoServer::register_routes() {
    auto& svr = impl_->svr;

    // ---- CORS 中间件 ----
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        if (req.method == "OPTIONS") {
            res.status = 204;
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // ---- 静态文件服务 ----
    std::string web_root = web_root_;
    if (!web_root.empty()) {
        svr.set_mount_point("/", web_root);
    }

    // ---- 健康检查 ----
    svr.Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        json j;
        j["status"] = "ok";
        j["version"] = "1.0.0";
        res.set_content(j.dump(), "application/json");
    });

    // ---- 根路径引导 ----
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(
            "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\">"
            "<title>MediaGo</title><style>"
            "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
            "display:flex;justify-content:center;align-items:center;height:100vh;"
            "margin:0;background:#f5f7fa;color:#2c3e50}"
            ".card{text-align:center;padding:48px;background:#fff;border-radius:12px;"
            "box-shadow:0 2px 12px rgba(0,0,0,.08)}"
            "h1{font-size:28px;margin-bottom:8px}"
            "p{color:#909399;margin-bottom:24px}"
            "a{display:inline-block;padding:10px 24px;background:#409eff;color:#fff;"
            "text-decoration:none;border-radius:6px;font-size:15px}"
            "a:hover{background:#337ecc}"
            "code{background:#f0f0f0;padding:2px 6px;border-radius:4px}"
            "</style></head><body><div class=\"card\">"
            "<h1>MediaGo Server</h1>"
            "<p>后端 API 已就绪 | 前端开发地址:</p>"
            "<a href=\"http://localhost:5173\">打开前端 GUI</a>"
            "<p style=\"margin-top:16px;font-size:13px\">"
            "API 端点: <code>/api/health</code> <code>/api/upload</code> <code>/api/batch</code></p>"
            "</div></body></html>",
            "text/html; charset=utf-8");
    });

    // ---- 文件上传 ----
    svr.Post("/api/upload", [this](const httplib::Request& req, httplib::Response& res) {
        json result;
        result["files"] = json::array();

        // 确保上传目录存在
#ifdef _WIN32
        CreateDirectoryA(impl_->upload_dir.c_str(), nullptr);
#else
        system(("mkdir -p \"" + impl_->upload_dir + "\"").c_str());
#endif

        for (auto& file : req.form.files) {
            std::string dest = impl_->upload_dir + "/" + file.second.filename;

            // 去重：若文件已存在，添加序号
            int suffix = 1;
            std::string base = dest;
            while (std::ifstream(dest)) {
                size_t dot = base.rfind('.');
                if (dot != std::string::npos) {
                    dest = base.substr(0, dot) + "_" + std::to_string(suffix++) +
                           base.substr(dot);
                } else {
                    dest = base + "_" + std::to_string(suffix++);
                }
            }

            std::ofstream out(dest, std::ios::binary);
            if (out) {
                out.write(file.second.content.data(), file.second.content.size());
                out.close();
                result["files"].push_back(dest);
            }
        }

        res.set_content(result.dump(), "application/json");
    });

    // ---- 批量转码 ----
    svr.Post("/api/batch", [this](const httplib::Request& req, httplib::Response& res) {
        // 解析请求体 JSON
        json req_body;
        try {
            req_body = json::parse(req.body);
        } catch (const std::exception& e) {
            json err;
            err["error"] = std::string("JSON parse error: ") + e.what();
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            return;
        }

        // 将 JSON 写入临时清单文件
        std::string task_id = Impl::make_task_id();
        std::string manifest_path =
            impl_->manifest_dir + "/_manifest_" + task_id + ".json";
        {
            std::ofstream f(manifest_path);
            f << req_body.dump(2);
        }

        // 创建任务状态
        auto state = std::make_shared<Impl::TaskState>();
        {
            std::lock_guard<std::mutex> lock(impl_->tasks_mtx);
            impl_->tasks[task_id] = state;
        }

        // 异步执行批量处理
        std::thread([this, task_id, state, manifest_path]() {
            state->progress.status = TaskStatus::Running;
            state->update_seq++;
            state->cv.notify_all();

            BatchProcessor bp;
            bool ok = bp.process(
                manifest_path.c_str(),
                [state](unsigned index, unsigned total, JobStatus status,
                        const std::string& input, const std::string& output) {
                    std::lock_guard<std::mutex> lock(state->mtx);
                    state->progress.current_job = index;
                    state->progress.total_jobs = total;
                    state->progress.current_file = input;

                    switch (status) {
                    case JobStatus::Processing:
                        state->progress.job_status = "processing";
                        break;
                    case JobStatus::OK:
                        state->progress.job_status = "ok";
                        state->progress.ok_count++;
                        state->progress.result_files.push_back(output);
                        break;
                    case JobStatus::Fail:
                        state->progress.job_status = "fail";
                        state->progress.fail_count++;
                        state->progress.result_errors.push_back(output.empty() ? input : output);
                        break;
                    }
                    state->update_seq++;
                    state->cv.notify_all();
                });

            {
                std::lock_guard<std::mutex> lock(state->mtx);
                state->progress.status =
                    ok ? TaskStatus::Completed : TaskStatus::Failed;
                if (!ok) {
                    state->progress.error = bp.last_error().empty()
                        ? "batch processing had failures"
                        : bp.last_error();
                }
                state->done = true;
                state->update_seq++;
                state->cv.notify_all();
            }

            // 清理清单文件
            std::remove(manifest_path.c_str());

            // 保存处理历史
            {
                json entry;
                entry["id"]  = task_id;
                entry["time"] = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                entry["status"] = ok ? "completed" : "failed";
                entry["ok_count"] = state->progress.ok_count;
                entry["fail_count"] = state->progress.fail_count;
                entry["result_files"] = state->progress.result_files;
                entry["result_errors"] = state->progress.result_errors;
                entry["error"] = state->progress.error;

                // 读取已有历史
                json history = json::array();
                {
                    std::ifstream hf(impl_->history_path);
                    if (hf) {
                        try { history = json::parse(hf); }
                        catch (...) { history = json::array(); }
                    }
                }
                history.push_back(entry);

                // 限制最多保留 100 条
                while (history.size() > 100) {
                    history.erase(history.begin());
                }

                std::ofstream hf(impl_->history_path);
                hf << history.dump(2);
            }
        }).detach();

        // 返回任务 ID
        json resp_body;
        resp_body["task_id"] = task_id;
        resp_body["status"] = "started";
        res.set_content(resp_body.dump(), "application/json");
    });

    // ---- SSE 进度推送 ----
    svr.Get(R"(/api/progress/([a-f0-9]+))", [this](const httplib::Request& req,
                                                     httplib::Response& res) {
        std::string task_id = req.matches[1];

        // 查找任务
        std::shared_ptr<Impl::TaskState> state;
        {
            std::lock_guard<std::mutex> lock(impl_->tasks_mtx);
            auto it = impl_->tasks.find(task_id);
            if (it == impl_->tasks.end()) {
                res.status = 404;
                json err;
                err["error"] = "task not found";
                res.set_content(err.dump(), "application/json");
                return;
            }
            state = it->second;
        }

        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("X-Accel-Buffering", "no");

        // SSE 连接本地共享状态
        struct SseContext {
            std::shared_ptr<Impl::TaskState> state;
            int last_seq = -1;   // 上次已发送的序号
            bool done_sent = false;
        };
        auto ctx = std::make_shared<SseContext>();
        ctx->state = state;

        auto provider = [ctx](size_t /*offset*/, httplib::DataSink& sink) {
            std::unique_lock<std::mutex> lock(ctx->state->mtx);

            // 等待有新数据或任务完成
            ctx->state->cv.wait(lock, [&ctx]() {
                return ctx->state->done ||
                       ctx->last_seq != ctx->state->update_seq;
            });

            // 任务完成且已发送过最终事件
            if (ctx->done_sent) {
                sink.done();
                return false;
            }

            // 已到最新数据
            if (ctx->last_seq == ctx->state->update_seq && !ctx->state->done) {
                return true; // 虚假唤醒，等待下次
            }

            ctx->last_seq = ctx->state->update_seq;

            // 构造进度 JSON（中间和最终共用）
            auto build_json = [&ctx]() {
                json j;
                j["status"] = ctx->state->done
                    ? (ctx->state->progress.status == TaskStatus::Completed
                           ? "completed"
                           : "failed")
                    : "running";
                j["current_job"] = ctx->state->progress.current_job;
                j["total_jobs"] = ctx->state->progress.total_jobs;
                j["current_file"] = ctx->state->progress.current_file;
                j["job_status"] = ctx->state->progress.job_status;
                j["ok_count"] = ctx->state->progress.ok_count;
                j["fail_count"] = ctx->state->progress.fail_count;
                if (ctx->state->done) {
                    j["error"] = ctx->state->progress.error;
                    j["result_files"] = ctx->state->progress.result_files;
                    j["result_errors"] = ctx->state->progress.result_errors;
                }
                return j;
            };

            if (ctx->state->done) {
                // 发送最终状态
                json j = build_json();
                std::string data = "data: " + j.dump() + "\n\n";
                sink.write(data.data(), data.size());
                sink.write("event: done\ndata: {}\n\n", 22);
                ctx->done_sent = true;
                sink.done();
                return false;
            }

            // 推送当前进度
            json j = build_json();

            std::string data = "data: " + j.dump() + "\n\n";
            sink.write(data.data(), data.size());

            return true; // 保持连接
        };

        res.set_chunked_content_provider("text/event-stream", provider);
    });

    // ---- 媒体探测 ----
    svr.Get("/api/probe", [](const httplib::Request& req, httplib::Response& res) {
        std::string path;
        if (req.has_param("path")) {
            path = req.get_param_value("path");
        } else {
            res.status = 400;
            json err;
            err["error"] = "missing 'path' query parameter";
            res.set_content(err.dump(), "application/json");
            return;
        }

        // ---- 路径清理：去除 Windows 复制路径引入的 Unicode 不可见控制字符 ----
        {
            // 移除常见的 Unicode 方向标记（UTF-8 编码）:
            //   U+200E  LTR Mark    E2 80 8E
            //   U+200F  RTL Mark    E2 80 8F
            //   U+202A  LRE         E2 80 AA
            //   U+202B  RLE         E2 80 AB
            //   U+202C  PDF         E2 80 AC
            //   U+202D  LRO         E2 80 AD
            //   U+202E  RLO         E2 80 AE
            //   U+FEFF  BOM / ZWNBS EF BB BF
            //   U+200B  ZWSP        E2 80 8B
            //   U+200C  ZWNJ        E2 80 8C
            //   U+200D  ZWJ         E2 80 8D
            const char* patterns[] = {
                "\xE2\x80\x8E", "\xE2\x80\x8F",
                "\xE2\x80\xAA", "\xE2\x80\xAB", "\xE2\x80\xAC",
                "\xE2\x80\xAD", "\xE2\x80\xAE",
                "\xEF\xBB\xBF",
                "\xE2\x80\x8B", "\xE2\x80\x8C", "\xE2\x80\x8D",
            };
            for (const char* p : patterns) {
                size_t len = std::strlen(p);
                size_t pos = 0;
                while ((pos = path.find(p, pos)) != std::string::npos) {
                    path.erase(pos, len);
                }
            }
        }

        // 去除首尾空格和引号
        while (!path.empty() && (path.front() == ' ' || path.front() == '"'))
            path.erase(0, 1);
        while (!path.empty() && (path.back() == ' ' || path.back() == '"'))
            path.pop_back();

        if (path.empty()) {
            res.status = 400;
            json err;
            err["error"] = "path is empty after sanitization";
            res.set_content(err.dump(), "application/json");
            return;
        }

        SourceInfo info;
        if (!media_probe(path.c_str(), &info)) {
            res.status = 404;
            json err;
            err["error"] = "cannot probe file: " + path;
            res.set_content(err.dump(), "application/json");
            return;
        }

        json j;
        j["codec_name"] = info.codec_name;
        j["codec_id"] = info.codec_id;
        j["width"] = info.width;
        j["height"] = info.height;
        j["pix_fmt"] = info.pix_fmt;
        j["pix_fmt_name"] = info.pix_fmt_name;
        j["bit_depth"] = info.bit_depth;
        j["color_space"] = info.color_space;
        j["color_range"] = info.color_range;
        j["container"] = info.container;
        j["has_icc"] = info.has_icc;
        j["has_alpha"] = info.has_alpha;
        j["nb_streams"] = info.nb_streams;
        j["is_image"] = info.is_image;

        res.set_content(j.dump(), "application/json");
    });

    // ---- 编解码器列表 ----
    svr.Get("/api/codecs", [](const httplib::Request& req, httplib::Response& res) {
        bool video_only = true;
        if (req.has_param("type") && req.get_param_value("type") == "audio") {
            video_only = false;
        }

        CodecInfo codecs[200];
        int n = config_list_codecs(codecs, 200, true, video_only);

        json j;
        j["codecs"] = json::array();
        j["total"] = n;
        for (int i = 0; i < n; i++) {
            json c;
            c["name"] = codecs[i].name;
            c["long_name"] = codecs[i].long_name;
            c["type"] = codecs[i].type;
            c["is_hardware"] = codecs[i].is_hardware;
            j["codecs"].push_back(c);
        }
        res.set_content(j.dump(), "application/json");
    });

    // ---- 像素格式列表 ----
    svr.Get("/api/pixfmts", [](const httplib::Request&, httplib::Response& res) {
        PixelFmtInfo fmts[200];
        int n = config_list_pixel_fmts(fmts, 200);

        json j;
        j["formats"] = json::array();
        j["total"] = n;
        for (int i = 0; i < n; i++) {
            json f;
            f["name"] = fmts[i].name;
            f["bits_per_pixel"] = fmts[i].bits_per_pixel;
            // 计算色度子采样标签
            std::string chroma;
            if (fmts[i].log2_chroma_w == 0 && fmts[i].log2_chroma_h == 0)
                chroma = "4:4:4";
            else if (fmts[i].log2_chroma_w == 1 && fmts[i].log2_chroma_h == 0)
                chroma = "4:2:2";
            else if (fmts[i].log2_chroma_w == 1 && fmts[i].log2_chroma_h == 1)
                chroma = "4:2:0";
            else
                chroma = "?";
            f["chroma"] = chroma;
            j["formats"].push_back(f);
        }
        res.set_content(j.dump(), "application/json");
    });

    // ---- 处理历史 ----
    svr.Get("/api/history", [this](const httplib::Request&, httplib::Response& res) {
        json history = json::array();
        {
            std::ifstream hf(impl_->history_path);
            if (hf) {
                try { history = json::parse(hf); } catch (...) {}
            }
        }
        res.set_content(history.dump(), "application/json");
    });

    // ---- 打开输出目录 ----
    svr.Post("/api/open-folder", [this](const httplib::Request& req, httplib::Response& res) {
        json req_body;
        try { req_body = json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"invalid json\"}", "application/json");
            return;
        }

        std::string dir = req_body.value("dir", "");
        if (dir.empty()) {
            res.status = 400;
            res.set_content("{\"ok\":false,\"error\":\"missing dir\"}", "application/json");
            return;
        }

#ifdef _WIN32
        // 安全检查：只允许 data/ 下的目录
        if (dir.rfind("data/", 0) != 0 && dir.rfind("data\\", 0) != 0 &&
            dir.rfind("./data/", 0) != 0 && dir.rfind(".\\data\\", 0) != 0) {
            res.status = 403;
            res.set_content("{\"ok\":false,\"error\":\"forbidden path\"}", "application/json");
            return;
        }

        // 确保目录存在
        CreateDirectoryA(dir.c_str(), nullptr);

        // 用 Explorer 打开
        std::string cmd = "explorer \"" + dir + "\"";
        system(cmd.c_str());
        res.set_content("{\"ok\":true}", "application/json");
#else
        std::string cmd = "open \"" + dir + "\"";
        system(cmd.c_str());
        res.set_content("{\"ok\":true}", "application/json");
#endif
    });

    // ---- 编码器参数能力查询 (视频) ----
    svr.Post("/api/encoder-params", [](const httplib::Request& req, httplib::Response& res) {
        json req_body;
        try { req_body = json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid json\"}", "application/json");
            return;
        }

        std::string codec = req_body.value("codec", "");
        if (codec.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"missing codec\"}", "application/json");
            return;
        }

        json result;
        result["codec"] = codec;

        struct EncoderProfile {
            std::vector<const char*> rate_controls; // crf / cqp / abr / cbr / vbr
            std::vector<const char*> presets;
            std::vector<const char*> tunes;
            std::vector<const char*> profiles;       // baseline / main / high ...
            std::vector<const char*> pixel_fmts;
        };

        std::map<std::string, EncoderProfile> profiles;

        // ---- H.264 系列 ----
        profiles["libx264"] = {
            {"crf","abr","cbr","vbr"},
            {"ultrafast","superfast","veryfast","faster","fast","medium",
             "slow","slower","veryslow","placebo"},
            {"film","animation","grain","stillimage","fastdecode","zerolatency"},
            {"baseline","main","high","high10","high422","high444"},
            {"yuv420p","yuv422p","yuv444p","yuv420p10le","yuv422p10le","yuv444p10le"}
        };
        profiles["libx264rgb"] = {
            {"crf","abr","cbr","vbr"},
            {"ultrafast","superfast","veryfast","faster","fast","medium",
             "slow","slower","veryslow","placebo"},
            {},
            {"baseline","main","high","high10","high422","high444"},
            {"rgb24","bgr0"}
        };
        profiles["h264_amf"] = {
            {"cqp","cbr","vbr"},
            {"speed","balanced","quality"},
            {},
            {"main","high"},
            {"nv12","yuv420p"}
        };
        profiles["h264_nvenc"] = {
            {"cqp","cbr","vbr"},
            {"p1","p2","p3","p4","p5","p6","p7"},
            {"hq","ll","ull","lossless"},
            {"baseline","main","high","high444p"},
            {"yuv420p","yuv444p","p010le"}
        };
        profiles["h264_qsv"] = {
            {"cqp","cbr","vbr"},
            {"veryfast","faster","fast","medium","slow","veryslow"},
            {},
            {"baseline","main","high"},
            {"nv12","yuv420p"}
        };

        // ---- H.265 / HEVC 系列 ----
        profiles["libx265"] = {
            {"crf","abr","cbr","vbr"},
            {"ultrafast","superfast","veryfast","faster","fast","medium",
             "slow","slower","veryslow","placebo"},
            {"psnr","ssim","grain","fastdecode","zerolatency"},
            {"main","main10","main12","main422-10","main422-12","main444-8","main444-10","main444-12"},
            {"yuv420p","yuv422p","yuv444p","yuv420p10le","yuv422p10le","yuv444p10le","yuv420p12le","yuv422p12le","yuv444p12le"}
        };
        profiles["hevc_amf"] = {
            {"cqp","cbr","vbr"},
            {"speed","balanced","quality"},
            {},
            {"main"},
            {"nv12","yuv420p"}
        };
        profiles["hevc_nvenc"] = {
            {"cqp","cbr","vbr"},
            {"p1","p2","p3","p4","p5","p6","p7"},
            {"hq","ll","ull","lossless"},
            {"main","main10","rext"},
            {"yuv420p","yuv444p","p010le","p016le"}
        };
        profiles["hevc_qsv"] = {
            {"cqp","cbr","vbr"},
            {"veryfast","faster","fast","medium","slow","veryslow"},
            {},
            {"main","main10","mainsp"},
            {"nv12","yuv420p","p010le"}
        };

        // ---- VP9 ----
        profiles["libvpx-vp9"] = {
            {"crf","abr","cbr","vbr"},
            {"0","1","2","3","4","5","6"},
            {},
            {"0","1","2","3"},
            {"yuv420p","yuv422p","yuv444p","yuv420p10le"}
        };

        // ---- AV1 ----
        profiles["libaom-av1"] = {
            {"crf","abr","cbr","vbr"},
            {"0","1","2","3","4","5","6","7","8","9"},
            {},
            {"0","1","2"},
            {"yuv420p","yuv422p","yuv444p","yuv420p10le","yuv420p12le"}
        };
        profiles["libsvtav1"] = {
            {"crf","abr","cbr","vbr"},
            {"0","1","2","3","4","5","6","7","8","9","10","11","12","13"},
            {},
            {"main","high","professional"},
            {"yuv420p","yuv420p10le"}
        };

        // ---- MPEG ----
        profiles["mpeg4"] = {
            {"abr","cbr"},
            {},
            {},
            {"simple","advanced_simple"},
            {"yuv420p"}
        };
        profiles["libxvid"] = {
            {"abr","cbr"},
            {},
            {},
            {},
            {"yuv420p"}
        };

        // ---- MJPEG ----
        profiles["mjpeg"] = {
            {"abr"},
            {},
            {},
            {},
            {"yuvj420p","yuvj422p","yuvj444p"}
        };

        auto it = profiles.find(codec);
        if (it != profiles.end()) {
            auto& p = it->second;

            json rc = json::array();
            for (auto& s : p.rate_controls) rc.push_back(s);
            result["rate_controls"] = rc;

            json pa = json::array();
            for (auto& s : p.presets) pa.push_back(s);
            result["presets"] = pa;

            json ta = json::array();
            for (auto& s : p.tunes) ta.push_back(s);
            result["tunes"] = ta;

            json pf = json::array();
            for (auto& s : p.profiles) pf.push_back(s);
            result["profiles"] = pf;

            json px = json::array();
            for (auto& s : p.pixel_fmts) px.push_back(s);
            result["pixel_fmts"] = px;
        } else {
            result["rate_controls"] = json::array({"abr"});
            result["presets"] = json::array();
            result["tunes"] = json::array();
            result["profiles"] = json::array();
            result["pixel_fmts"] = json::array();
        }

        res.set_content(result.dump(), "application/json");
    });

    // ---- 音频编码器参数能力查询 ----
    svr.Post("/api/audio-encoder-params", [](const httplib::Request& req, httplib::Response& res) {
        json req_body;
        try { req_body = json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"invalid json\"}", "application/json");
            return;
        }

        std::string codec = req_body.value("codec", "");
        if (codec.empty()) {
            res.status = 400;
            res.set_content("{\"error\":\"missing codec\"}", "application/json");
            return;
        }

        json result;
        result["codec"] = codec;

        struct AudioProfile {
            std::vector<int> sample_rates;
            std::vector<const char*> channel_layouts;
            bool has_quality;
            bool has_bitrate;
        };

        std::map<std::string, AudioProfile> aprofiles;
        aprofiles["aac"] = {
            {8000,11025,12000,16000,22050,24000,32000,44100,48000,88200,96000},
            {"mono","stereo","5.1","7.1"},
            true, true
        };
        aprofiles["libfdk_aac"] = {
            {8000,11025,12000,16000,22050,24000,32000,44100,48000,88200,96000},
            {"mono","stereo","5.1","7.1"},
            true, true
        };
        aprofiles["libmp3lame"] = {
            {8000,11025,12000,16000,22050,24000,32000,44100,48000},
            {"mono","stereo","joint_stereo"},
            false, true
        };
        aprofiles["flac"] = {
            {8000,16000,22050,24000,32000,44100,48000,88200,96000},
            {"mono","stereo","5.1","7.1"},
            false, false
        };
        aprofiles["libopus"] = {
            {8000,12000,16000,24000,48000},
            {"mono","stereo","5.1","7.1"},
            false, true
        };
        aprofiles["ac3"] = {
            {32000,44100,48000},
            {"mono","stereo","5.1"},
            false, true
        };
        aprofiles["libvorbis"] = {
            {8000,11025,16000,22050,44100,48000},
            {"mono","stereo","5.1"},
            true, true
        };
        aprofiles["pcm_s16le"] = {
            {8000,16000,22050,24000,32000,44100,48000,88200,96000},
            {"mono","stereo","5.1","7.1"},
            false, false
        };
        aprofiles["pcm_s24le"] = {
            {8000,16000,22050,24000,32000,44100,48000,88200,96000},
            {"mono","stereo","5.1","7.1"},
            false, false
        };

        auto it = aprofiles.find(codec);
        if (it != aprofiles.end()) {
            auto& p = it->second;

            json srs = json::array();
            for (auto& sr : p.sample_rates) srs.push_back(sr);
            result["sample_rates"] = srs;

            json cls = json::array();
            for (auto& cl : p.channel_layouts) cls.push_back(cl);
            result["channel_layouts"] = cls;

            result["has_quality"] = p.has_quality;
            result["has_bitrate"] = p.has_bitrate;
        } else {
            result["sample_rates"] = json::array({44100, 48000});
            result["channel_layouts"] = json::array({"stereo"});
            result["has_quality"] = false;
            result["has_bitrate"] = true;
        }

        res.set_content(result.dump(), "application/json");
    });
}

// ============================================================
// 启动 / 停止
// ============================================================

bool MediaGoServer::start(int port, const char* web_root) {
    if (running_) return true;

    port_ = port;
    if (web_root) web_root_ = web_root;

    // 初始化数据子目录
    impl_->upload_dir   = impl_->data_dir + "/uploads";
    impl_->manifest_dir = impl_->data_dir + "/manifests";
    impl_->history_path = impl_->data_dir + "/history.json";

    register_routes();

    running_ = true;

    // 确保数据目录存在
#ifdef _WIN32
    CreateDirectoryA(impl_->data_dir.c_str(), nullptr);
    CreateDirectoryA(impl_->upload_dir.c_str(), nullptr);
    CreateDirectoryA(impl_->manifest_dir.c_str(), nullptr);
#else
    system(("mkdir -p \"" + impl_->data_dir + "\"").c_str());
    system(("mkdir -p \"" + impl_->upload_dir + "\"").c_str());
    system(("mkdir -p \"" + impl_->manifest_dir + "\"").c_str());
#endif

    server_thread_ = std::thread([this]() {
        impl_->svr.listen("127.0.0.1", port_);
        running_ = false;
    });

    // 等待服务启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    return impl_->svr.is_running();
}

void MediaGoServer::stop() {
    if (!running_) return;

    impl_->svr.stop();

    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    running_ = false;
}
