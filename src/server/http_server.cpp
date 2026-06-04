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
    std::string upload_dir = "./uploads";

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
            impl_->upload_dir + "/_manifest_" + task_id + ".json";
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
                        const std::string& input) {
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
                        state->progress.result_files.push_back(input);
                        break;
                    case JobStatus::Fail:
                        state->progress.job_status = "fail";
                        state->progress.fail_count++;
                        state->progress.result_errors.push_back(input);
                        break;
                    }
                    state->update_seq++;
                    state->cv.notify_all();
                });

            {
                std::lock_guard<std::mutex> lock(state->mtx);
                state->progress.status =
                    ok ? TaskStatus::Completed : TaskStatus::Failed;
                if (!ok)
                    state->progress.error = "batch processing had failures";
                state->done = true;
                state->update_seq++;
                state->cv.notify_all();
            }

            // 清理清单文件
            std::remove(manifest_path.c_str());
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

            if (ctx->state->done) {
                // 发送最终状态
                json j;
                j["status"] = ctx->state->progress.status == TaskStatus::Completed
                                  ? "completed"
                                  : "failed";
                j["current_job"] = ctx->state->progress.current_job;
                j["total_jobs"] = ctx->state->progress.total_jobs;
                j["current_file"] = ctx->state->progress.current_file;
                j["job_status"] = ctx->state->progress.job_status;
                j["ok_count"] = ctx->state->progress.ok_count;
                j["fail_count"] = ctx->state->progress.fail_count;
                j["error"] = ctx->state->progress.error;
                std::string data = "data: " + j.dump() + "\n\n";
                sink.write(data.data(), data.size());
                sink.write("event: done\ndata: {}\n\n", 22);
                ctx->done_sent = true;
                sink.done();
                return false;
            }

            // 推送当前进度
            json j;
            j["status"] = "running";
            j["current_job"] = ctx->state->progress.current_job;
            j["total_jobs"] = ctx->state->progress.total_jobs;
            j["current_file"] = ctx->state->progress.current_file;
            j["job_status"] = ctx->state->progress.job_status;
            j["ok_count"] = ctx->state->progress.ok_count;
            j["fail_count"] = ctx->state->progress.fail_count;

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
}

// ============================================================
// 启动 / 停止
// ============================================================

bool MediaGoServer::start(int port, const char* web_root) {
    if (running_) return true;

    port_ = port;
    if (web_root) web_root_ = web_root;

    register_routes();

    running_ = true;

    // 确保上传目录存在
#ifdef _WIN32
    CreateDirectoryA(impl_->upload_dir.c_str(), nullptr);
#else
    system(("mkdir -p \"" + impl_->upload_dir + "\"").c_str());
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
