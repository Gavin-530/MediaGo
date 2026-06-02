// MediaGo - 环境诊断工具（开发用，发布时通过 Makefile target 排除）
// 编译: mingw32-make diag

#include "core/diag.h"

int main() {
    diag_run_all();
    return 0;
}
