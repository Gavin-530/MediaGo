<template>
  <div class="batch-page">
    <h1 class="page-title">批量处理</h1>

    <!-- 步骤 1: 文件上传 -->
    <FileUploader v-model="selectedFiles" />

    <!-- 步骤 2: 输出与转码配置 -->
    <el-card class="config-card" shadow="never">
      <template #header><span>输出配置</span></template>
      <el-form :model="config" label-width="100px" size="default">
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="输出目录">
              <el-input v-model="config.outputDir" placeholder="./output" />
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="目录结构">
              <el-select v-model="config.structure">
                <el-option label="按类型分目录" value="by_type" />
                <el-option label="平铺" value="flat" />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>

        <el-divider content-position="left">视频编码</el-divider>
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="编码器">
              <el-select v-model="config.videoCodec" clearable filterable placeholder="默认 = 流拷贝">
                <el-option
                  v-for="c in videoCodecs"
                  :key="c.name"
                  :label="`${c.name} (${c.long_name})`"
                  :value="c.name"
                />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="6">
            <el-form-item label="CRF 质量">
              <el-input-number v-model="config.videoCrf" :min="0" :max="51" />
            </el-form-item>
          </el-col>
          <el-col :span="6">
            <el-form-item label="码率 (bps)">
              <el-input v-model="config.videoBitrate" placeholder="如 2000000" />
            </el-form-item>
          </el-col>
        </el-row>
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="编码预设">
              <el-select v-model="config.videoPreset" clearable placeholder="默认">
                <el-option label="ultrafast" value="ultrafast" />
                <el-option label="fast" value="fast" />
                <el-option label="medium" value="medium" />
                <el-option label="slow" value="slow" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="缩放">
              <el-input v-model="config.videoScale" placeholder="如 1920x1080" />
            </el-form-item>
          </el-col>
        </el-row>

        <el-divider content-position="left">音频编码</el-divider>
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="编码器">
              <el-select v-model="config.audioCodec" clearable filterable placeholder="默认 = 流拷贝">
                <el-option label="AAC" value="aac" />
                <el-option label="MP3" value="libmp3lame" />
                <el-option label="FLAC" value="flac" />
                <el-option label="Opus" value="libopus" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="码率 (bps)">
              <el-input v-model="config.audioBitrate" placeholder="如 128000" />
            </el-form-item>
          </el-col>
        </el-row>

        <el-divider />
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="容器格式">
              <el-select v-model="config.format" clearable placeholder="自动">
                <el-option label="MP4" value="mp4" />
                <el-option label="MKV" value="mkv" />
                <el-option label="MOV" value="mov" />
                <el-option label="AVI" value="avi" />
                <el-option label="WebM" value="webm" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col :span="12">
            <el-form-item label="覆盖已存在">
              <el-switch v-model="config.overwrite" />
            </el-form-item>
          </el-col>
        </el-row>
      </el-form>
    </el-card>

    <!-- 步骤 3: 开始处理 -->
    <div class="action-bar">
      <el-button
        type="primary"
        size="large"
        :disabled="selectedFiles.length === 0 || processing"
        :loading="processing"
        @click="startBatch"
      >
        <el-icon><VideoPlay /></el-icon>
        开始处理
      </el-button>
      <el-button
        v-if="selectedFiles.length > 0 && !processing"
        size="large"
        @click="clearFiles"
      >
        清空文件
      </el-button>
    </div>

    <!-- 进度面板 -->
    <ProgressPanel
      :visible="processing || taskDone"
      :progress="progress"
    />

    <!-- 处理结果 -->
    <el-card v-if="taskDone" class="result-card" shadow="never">
      <template #header><span>处理结果</span></template>
      <el-result
        :icon="progress.status === 'completed' ? 'success' : 'error'"
        :title="progress.status === 'completed' ? '处理完成' : '处理失败'"
        :sub-title="`成功 ${progress.ok_count} / 失败 ${progress.fail_count}`"
      >
        <template #extra>
          <el-button type="primary" @click="resetTask">
            开始新任务
          </el-button>
        </template>
      </el-result>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { uploadFiles, submitBatch, getCodecs } from '../api'
import { useSSE, type SseEvent } from '../composables/useSSE'
import FileUploader, { type UploadFile } from '../components/FileUploader.vue'
import ProgressPanel, { type ProgressState } from '../components/ProgressPanel.vue'

// ---- 文件 ----
const selectedFiles = ref<UploadFile[]>([])

// ---- 配置 ----
const config = reactive({
  outputDir: './output',
  structure: 'by_type',
  videoCodec: '',
  videoCrf: 23,
  videoBitrate: '',
  videoPreset: '',
  videoScale: '',
  audioCodec: '',
  audioBitrate: '',
  format: '',
  overwrite: false,
})

// ---- 可用编解码器 ----
const videoCodecs = ref<
  { name: string; long_name: string; is_hardware: boolean }[]
>([])

onMounted(async () => {
  try {
    const resp = await getCodecs('video')
    videoCodecs.value = resp.data.codecs || []
  } catch {
    // fallback
  }
})

// ---- 进度 ----
const processing = ref(false)
const taskDone = ref(false)

const progress = reactive<ProgressState>({
  status: '',
  current_job: 0,
  total_jobs: 0,
  current_file: '',
  job_status: '',
  ok_count: 0,
  fail_count: 0,
})

function onProgress(e: SseEvent) {
  Object.assign(progress, e)
}

const { connect: connectSSE } = useSSE(onProgress, () => {
  processing.value = false
  taskDone.value = true
})

// ---- 开始批量处理 ----
async function startBatch() {
  if (selectedFiles.value.length === 0) {
    ElMessage.warning('请先选择文件')
    return
  }

  processing.value = true
  taskDone.value = false

  try {
    // 1. 上传文件
    const formData = new FormData()
    selectedFiles.value.forEach((f) => {
      formData.append('files', f.raw)
    })
    const uploadResp = await uploadFiles(formData)
    const uploadedPaths: string[] = uploadResp.data.files || []

    if (uploadedPaths.length === 0) {
      ElMessage.error('上传失败')
      processing.value = false
      return
    }

    // 2. 构建批量清单
    const manifest: any = {
      output: {
        dir: config.outputDir || './output',
        structure: config.structure,
      },
      defaults: {
        overwrite: config.overwrite,
      },
      jobs: uploadedPaths.map((path: string) => {
        const job: any = { input: path }

        // 视频配置
        const video: any = {}
        if (config.videoCodec) video.codec = config.videoCodec
        if (config.videoCrf >= 0) video.crf = config.videoCrf
        if (config.videoBitrate) video.bitrate = parseInt(config.videoBitrate)
        if (config.videoPreset) video.preset = config.videoPreset
        if (config.videoScale) {
          const [w, h] = config.videoScale.split('x').map(Number)
          if (w) video.width = w
          if (h) video.height = h
        }
        if (Object.keys(video).length > 0) job.video = video

        // 音频配置
        const audio: any = {}
        if (config.audioCodec) audio.codec = config.audioCodec
        if (config.audioBitrate) audio.bitrate = parseInt(config.audioBitrate)
        if (Object.keys(audio).length > 0) job.audio = audio

        // 容器
        if (config.format) job.format = config.format

        return job
      }),
    }

    // 3. 提交批量任务
    const batchResp = await submitBatch(manifest)
    const taskId: string = batchResp.data.task_id

    // 4. 连接 SSE 监控进度
    connectSSE(taskId)
  } catch (err: any) {
    ElMessage.error(err.response?.data?.error || err.message || '处理失败')
    processing.value = false
  }
}

function clearFiles() {
  selectedFiles.value = []
}

function resetTask() {
  taskDone.value = false
  Object.assign(progress, {
    status: '',
    current_job: 0,
    total_jobs: 0,
    current_file: '',
    job_status: '',
    ok_count: 0,
    fail_count: 0,
    error: '',
  })
}
</script>

<style scoped>
.batch-page {
  max-width: 960px;
  margin: 0 auto;
}

.page-title {
  font-size: 22px;
  font-weight: 600;
  margin-bottom: 20px;
  color: #1a1a2e;
}

.config-card {
  margin-top: 16px;
}

.action-bar {
  margin-top: 20px;
  display: flex;
  gap: 12px;
  align-items: center;
}

.result-card {
  margin-top: 20px;
}
</style>
