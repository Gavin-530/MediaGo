#pragma once

// 打印 FFmpeg 版本信息
void diag_print_version();

// 列出硬件编码器
void diag_list_hw_encoders();

// 列出硬件解码器
void diag_list_hw_decoders();

// 检查 VMAF 是否可用
void diag_check_vmaf();

// 打印全部编译配置
void diag_print_config();

// 完整诊断报告
void diag_run_all();
