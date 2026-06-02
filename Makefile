# ============================================================
# MediaGo - Makefile (MinGW / g++)
# ============================================================

CXX      := g++
CC       := gcc
SRCDIR   := src
BUILDDIR := build

# FFmpeg（通过 scripts/setup_ffmpeg.ps1 下载）
FFMPEG_DIR := libs/ffmpeg
FFMPEG_INC := $(FFMPEG_DIR)/include
FFMPEG_LIB := $(FFMPEG_DIR)/lib

# FFmpeg 库模块
FFMPEG_LIBS := -lavcodec -lavformat -lavutil -lswscale -lswresample -lavfilter

CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -D__STDC_CONSTANT_MACROS -I$(FFMPEG_INC)
LDFLAGS  := -static -L$(FFMPEG_LIB) $(FFMPEG_LIBS)
LDFLAGS  += -lbcrypt -lole32 -lws2_32 -lsecur32 -lmfuuid -lstrmiids -luuid

# ============================================================
# 模块清单
# ============================================================

# APP: 主程序
APP_SRCS := $(SRCDIR)/main.cpp \
            $(SRCDIR)/core/media_io.cpp \
            $(SRCDIR)/core/transcoder.cpp

# DIAG: 诊断工具（开发用，不发布）
DIAG_SRCS := $(SRCDIR)/diag_main.cpp \
             $(SRCDIR)/core/diag.cpp

# TEST: 5个独立测试，各验证一个维度
#   test_codec   - 编解码器/滤镜可用性（纯 FFmpeg，无主体依赖）
#   test_quality - PSNR/SSIM/VMAF 滤镜管线（纯 FFmpeg，无主体依赖）
#   test_image   - 图片格式转换（依赖 media_io + transcoder）
#   test_svg     - SVG 光栅化（依赖 media_io）
#   test_audio   - 音频编解码 round-trip（纯 FFmpeg，无主体依赖）
TEST_IMG_OBJS := $(BUILDDIR)/core/media_io.cpp.o \
                 $(BUILDDIR)/core/transcoder.cpp.o
TEST_SVG_OBJS := $(BUILDDIR)/core/media_io.cpp.o

# ============================================================
# 构建
# ============================================================
.PHONY: all diag test test_codec test_quality test_image test_svg test_audio clean run info

all: $(BUILDDIR)/MediaGo.exe
diag: $(BUILDDIR)/diag.exe

test: test_codec test_quality test_image test_svg test_audio
	@echo.
	@echo === All tests completed ===

test_codec: $(BUILDDIR)/test_codec.exe
	$(BUILDDIR)/test_codec.exe

test_quality: $(BUILDDIR)/test_quality.exe
	$(BUILDDIR)/test_quality.exe

test_image: $(BUILDDIR)/test_image.exe
	$(BUILDDIR)/test_image.exe

test_svg: $(BUILDDIR)/test_svg.exe
	$(BUILDDIR)/test_svg.exe

test_audio: $(BUILDDIR)/test_audio.exe
	$(BUILDDIR)/test_audio.exe

# ---- 辅助函数：src/*.cpp -> build/*.cpp.o
to_obj = $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%.o,$(1))

APP_OBJS  := $(call to_obj,$(APP_SRCS))
DIAG_OBJS := $(call to_obj,$(DIAG_SRCS))

$(BUILDDIR)/MediaGo.exe: $(APP_OBJS) | $(BUILDDIR)
	$(CXX) $(APP_OBJS) -o $@ $(LDFLAGS)

$(BUILDDIR)/diag.exe: $(DIAG_OBJS) | $(BUILDDIR)
	$(CXX) $(DIAG_OBJS) -o $@ $(LDFLAGS)

# ---- 测试程序规则 ----
# test_codec / test_quality：纯 FFmpeg，不需要主体模块
$(BUILDDIR)/test_codec.exe: $(BUILDDIR)/test_codec.o | $(BUILDDIR)
	$(CXX) $(BUILDDIR)/test_codec.o -o $@ $(LDFLAGS)

$(BUILDDIR)/test_quality.exe: $(BUILDDIR)/test_quality.o | $(BUILDDIR)
	$(CXX) $(BUILDDIR)/test_quality.o -o $@ $(LDFLAGS)

# test_image：依赖 media_io + transcoder
$(BUILDDIR)/test_image.exe: $(BUILDDIR)/test_image.o $(TEST_IMG_OBJS) | $(BUILDDIR)
	$(CXX) $(BUILDDIR)/test_image.o $(TEST_IMG_OBJS) -o $@ $(LDFLAGS)

# test_svg：只依赖 media_io
$(BUILDDIR)/test_svg.exe: $(BUILDDIR)/test_svg.o $(TEST_SVG_OBJS) | $(BUILDDIR)
	$(CXX) $(BUILDDIR)/test_svg.o $(TEST_SVG_OBJS) -o $@ $(LDFLAGS)

# test_audio：纯 FFmpeg，不需要主体模块
$(BUILDDIR)/test_audio.exe: $(BUILDDIR)/test_audio.o | $(BUILDDIR)
	$(CXX) $(BUILDDIR)/test_audio.o -o $@ $(LDFLAGS)

# tests/ 目录下的 .cpp -> build/*.o
$(BUILDDIR)/test_%.o: tests/test_%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c $< -o $@

# ---- 编译规则（自动处理 src/ 和 src/**/ 子目录）
$(BUILDDIR)/%.cpp.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/%.c.o: $(SRCDIR)/%.c | $(BUILDDIR)
	@if not exist "$(dir $@)" mkdir "$(dir $@)"
	$(CC) -std=c11 -Wall -Wextra -O2 -I$(FFMPEG_INC) -c $< -o $@

$(BUILDDIR):
	@if not exist "$(BUILDDIR)" mkdir "$(BUILDDIR)"

run: $(BUILDDIR)/MediaGo.exe
	$(BUILDDIR)/MediaGo.exe

clean:
	@if exist "$(BUILDDIR)" rmdir /s /q "$(BUILDDIR)"

info:
	@echo "=== FFmpeg Config ==="
	@echo "Include : $(FFMPEG_INC)"
	@echo "Lib     : $(FFMPEG_LIB)"
	@if exist "$(FFMPEG_INC)\libavcodec\avcodec.h" (echo "Headers : OK") else (echo "Headers : MISSING - run setup_ffmpeg.ps1")
	@if exist "$(FFMPEG_LIB)\libavcodec.dll.a" (echo "Libs    : OK") else (echo "Libs    : MISSING - run setup_ffmpeg.ps1")
