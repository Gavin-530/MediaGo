# MediaGo FFmpeg 环境配置脚本
# 运行: powershell -ExecutionPolicy Bypass -File scripts/setup_ffmpeg.ps1
#
# 下载 BtbN FFmpeg GPL 完整版，包含:
#   - x264, x265 (GPL)
#   - libvmaf (Netflix VMAF 质量评估)
#   - d3d11va, dxva2, vulkan 硬解
#   - av1 (dav1d), vp9, opus, vorbis 等
#   - 不含 fdk-aac（许可冲突，见下方说明）

param(
    [string]$Variant = "gpl-shared"  # gpl-shared | lgpl-shared
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$BaseUrl  = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest"
$FileName = "ffmpeg-master-latest-win64-$Variant.zip"
$Url      = "$BaseUrl/$FileName"
$LibsDir  = "$PSScriptRoot\..\libs"
$FfmpegDir = "$LibsDir\ffmpeg"

Write-Host "=== MediaGo FFmpeg Setup ===" -ForegroundColor Cyan
Write-Host "Variant: $Variant" -ForegroundColor Yellow
Write-Host "Target : $FfmpegDir"
Write-Host ""

# 检查是否已安装
if (Test-Path "$FfmpegDir\include\libavcodec\avcodec.h") {
    Write-Host "FFmpeg 已安装，跳过。如需重新安装请先删除 libs/ffmpeg/" -ForegroundColor Green
    exit 0
}

# 创建目录
New-Item -ItemType Directory -Force -Path $LibsDir  | Out-Null

# 下载
$ZipPath = "$LibsDir\ffmpeg.zip"
Write-Host "正在下载 $FileName ..." -ForegroundColor Yellow
Write-Host "地址: $Url" -ForegroundColor DarkGray
try {
    Invoke-WebRequest -Uri $Url -OutFile $ZipPath
} catch {
    Write-Host "下载失败: $_" -ForegroundColor Red
    exit 1
}
Write-Host "下载完成 ($((Get-Item $ZipPath).Length / 1MB) MB)" -ForegroundColor Green

# 解压
Write-Host "正在解压..." -ForegroundColor Yellow
Expand-Archive -Path $ZipPath -DestinationPath $LibsDir -Force

# 找到解压出的目录并重命名
$ExtractedDir = Get-ChildItem -Directory -Path $LibsDir | Where-Object { $_.Name -like "ffmpeg-*" } | Select-Object -First 1
if ($ExtractedDir) {
    if (Test-Path $FfmpegDir) { Remove-Item -Recurse -Force $FfmpegDir }
    Rename-Item -Path $ExtractedDir.FullName -NewName "ffmpeg"
}

# 清理
Remove-Item -Force $ZipPath
Write-Host "FFmpeg 安装完成!" -ForegroundColor Green
Write-Host ""

# 显示版本信息
$DllPath = "$FfmpegDir\bin\avcodec-*.dll"
$Dll = Get-Item $DllPath -ErrorAction SilentlyContinue
if ($Dll) {
    $Version = $Dll.Name -replace 'avcodec-', '' -replace '\.dll', ''
    Write-Host "libavcodec 版本: $Version" -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "=== 许可说明 ===" -ForegroundColor Cyan
Write-Host "本包为 GPL 许可，包含 x264/x265。分发时需保留 LICENSE 文件。" -ForegroundColor Yellow

Write-Host ""
Write-Host "=== fdk-aac 说明 ===" -ForegroundColor Cyan
Write-Host "fdk-aac 不包含在内。原因:" -ForegroundColor Yellow
Write-Host "  1. fdk-aac 许可与 GPL 库冲突，合并分发有法律风险" -ForegroundColor DarkGray
Write-Host "  2. Fraunhofer AAC 专利需单独商业授权" -ForegroundColor DarkGray
Write-Host "  3. FFmpeg 内置 AAC 编码器质量已接近 fdk-aac" -ForegroundColor DarkGray
Write-Host "Note: Use lgpl-shared variant for fdk-aac, but you will lose x264/x265/VMAF" -ForegroundColor DarkGray
