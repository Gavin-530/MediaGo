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

# FFmpeg 库模块（可按需删减）
FFMPEG_LIBS := -lavcodec -lavformat -lavutil -lswscale -lswresample -lavfilter

CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I$(FFMPEG_INC)
LDFLAGS  := -static -L$(FFMPEG_LIB) $(FFMPEG_LIBS)
LDFLAGS  += -lbcrypt -lole32 -lws2_32 -lsecur32 -lmfuuid -lstrmiids -luuid

# ============================================================
# 模块清单 —— 按需增删
# ============================================================

# APP: 主程序
APP_SRCS := $(SRCDIR)/main.cpp

# DIAG: 诊断工具（开发用，不发布）
DIAG_SRCS := $(SRCDIR)/diag_main.cpp \
             $(SRCDIR)/core/diag.cpp

# ============================================================
# 构建
# ============================================================
.PHONY: all diag clean run info

all: $(BUILDDIR)/MediaGo.exe
diag: $(BUILDDIR)/diag.exe

# ---- 辅助函数：src/*.cpp -> build/*.cpp.o
to_obj = $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%.o,$(1))

APP_OBJS  := $(call to_obj,$(APP_SRCS))
DIAG_OBJS := $(call to_obj,$(DIAG_SRCS))

$(BUILDDIR)/MediaGo.exe: $(APP_OBJS) | $(BUILDDIR)
	$(CXX) $(APP_OBJS) -o $@ $(LDFLAGS)

$(BUILDDIR)/diag.exe: $(DIAG_OBJS) | $(BUILDDIR)
	$(CXX) $(DIAG_OBJS) -o $@ $(LDFLAGS)

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
