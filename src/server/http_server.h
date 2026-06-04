// MediaGo — HTTP 服务层
// 基于 cpp-httplib 的本地 REST API 服务器
// 为 Vue 前端提供文件上传、批量转码、进度推送、媒体探测接口
//
// API 端点：
//   POST   /api/upload          — 上传文件到临时目录
//   POST   /api/batch           — 提交批量转码任务（JSON 清单）
//   GET    /api/progress/:id    — SSE 进度推送
//   GET    /api/probe           — 探测媒体文件属性 ?path=xxx
//   GET    /api/codecs          — 枚举可用编解码器
//   GET    /api/pixfmts         — 枚举可用像素格式
//   GET    /api/health          — 健康检查

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// 批量任务状态（供 SSE 推送）
// ============================================================
enum class TaskStatus { Pending, Running, Completed, Failed };

struct TaskProgress {
    TaskStatus status = TaskStatus::Pending;
    unsigned current_job = 0;
    unsigned total_jobs = 0;
    std::string current_file;
    std::string job_status;     // "processing" / "ok" / "fail"
    std::string error;
    // 累计结果
    unsigned ok_count = 0;
    unsigned fail_count = 0;
    std::vector<std::string> result_files;
    std::vector<std::string> result_errors;
};

// ============================================================
// HTTP 服务器
// ============================================================
class MediaGoServer {
public:
    MediaGoServer();
    ~MediaGoServer();

    // 启动服务（绑定 localhost:port）
    bool start(int port = 9527, const char* web_root = nullptr);

    // 停止服务
    void stop();

    // 是否运行中
    bool is_running() const { return running_; }

private:
    // ---- 路由注册 ----
    void register_routes();

    // ---- 内部实现 ----
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    int port_ = 9527;
    std::string web_root_;
};
