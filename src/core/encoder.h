#pragma once

// 简单编码模块（示例，可按需删减）

#ifdef __cplusplus
extern "C" {
#endif

// 使用 libx264 编码一段测试视频到文件
int encode_test_video(const char* output_path, int width, int height,
                      int fps, int duration_sec);

#ifdef __cplusplus
}
#endif
