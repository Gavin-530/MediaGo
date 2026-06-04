// MediaGo — HTTP 服务入口
// 启动本地 REST API 服务器，供前端 GUI 调用
//
// 用法:
//   MediaGoServer [--port 9527] [--web-root ./frontend/dist]

#include "server/http_server.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

static MediaGoServer* g_server = nullptr;

static void signal_handler(int sig) {
    if (g_server) {
        fprintf(stderr, "\n[MediaGo] shutting down (signal %d)...\n", sig);
        g_server->stop();
    }
}

int main(int argc, char** argv) {
    int port = 9527;
    const char* web_root = nullptr;

    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--web-root") && i + 1 < argc) {
            web_root = argv[++i];
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            puts("MediaGo Server — 本地 HTTP 服务");
            puts("");
            puts("用法: MediaGoServer [options]");
            puts("");
            puts("选项:");
            puts("  --port <PORT>       服务端口 (默认: 9527)");
            puts("  --web-root <DIR>    前端静态文件目录 (默认: 仅 API)");
            puts("  --help, -h          显示帮助");
            return 0;
        }
    }

    // 注册信号处理
    signal(SIGINT, signal_handler);
#ifdef SIGTERM
    signal(SIGTERM, signal_handler);
#endif

    MediaGoServer server;
    g_server = &server;

    printf("[MediaGo] starting HTTP server on http://127.0.0.1:%d\n", port);
    if (web_root) {
        printf("[MediaGo] serving static files from: %s\n", web_root);
    }

    if (!server.start(port, web_root)) {
        fprintf(stderr, "[MediaGo] failed to start server on port %d\n", port);
        return 1;
    }

    printf("[MediaGo] server is running. Press Ctrl+C to stop.\n");

    // 主线程等待（server_thread 由 start 创建，在 stop 时 join）
    while (server.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    printf("[MediaGo] server stopped.\n");
    return 0;
}
