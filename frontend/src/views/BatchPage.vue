<template>
  <div class="batch-page">
    <h1 class="page-title">批量处理</h1>

    <!-- 处理历史面板 -->
    <el-collapse v-if="history.length > 0" class="history-panel" v-model="historyOpen">
      <el-collapse-item name="1">
        <template #title>
          <span>处理历史 ({{ history.length }})</span>
        </template>
        <div class="history-list">
          <div
            v-for="(item, i) in history"
            :key="item.id"
            class="history-entry"
            :class="item.status === 'completed' ? 'hist-success' : 'hist-fail'"
          >
            <div class="hist-meta">
              <el-tag :type="item.status === 'completed' ? 'success' : 'danger'" size="small">
                {{ item.status === 'completed' ? '成功' : '失败' }}
              </el-tag>
              <span class="hist-time">{{ formatTime(item.time) }}</span>
              <span class="hist-count">
                {{ item.ok_count }}/{{ (item.ok_count || 0) + (item.fail_count || 0) }} 完成
              </span>
              <el-button
                v-if="item.result_files && item.result_files.length > 0"
                link
                size="small"
                type="primary"
                @click="openHistoryFolder(item)"
              >
                打开输出目录
              </el-button>
            </div>
            <div v-if="item.result_files && item.result_files.length > 0" class="hist-files">
              <span v-for="(f, j) in item.result_files" :key="'hf-'+j" class="hist-file">
                {{ f }}
              </span>
            </div>
          </div>
        </div>
      </el-collapse-item>
    </el-collapse>

    <!-- 步骤 1: 文件上传 -->
    <FileUploader v-model="selectedFiles" />

    <!-- 步骤 2: 输出与转码配置 -->
    <el-card class="config-card" shadow="never">
      <template #header><span>输出配置</span></template>
      <el-form :model="config" label-width="110px" size="default">
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="输出目录">
              <el-input v-model="config.outputDir" placeholder="./data/output" />
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

        <!-- ============ 视频编码 ============ -->
        <el-divider content-position="left">视频编码</el-divider>
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="编码器">
              <el-select v-model="config.videoCodec" clearable filterable placeholder="默认 = 流拷贝" @change="onVideoCodecChange">
                <el-option
                  v-for="c in videoCodecs"
                  :key="c.name"
                  :label="`${c.name} (${c.long_name})`"
                  :value="c.name"
                />
              </el-select>
            </el-form-item>
          </el-col>
          <!-- 码率控制模式 -->
          <el-col :span="12">
            <el-form-item v-if="encoderCaps.rate_controls.length > 0" label="码率控制">
              <el-select v-model="config.videoRateControl" @change="onRateControlChange">
                <el-option
                  v-for="rc in encoderCaps.rate_controls"
                  :key="rc"
                  :label="rateControlLabel(rc)"
                  :value="rc"
                />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>

        <!-- 码率控制参数（动态） -->
        <el-row v-if="config.videoCodec && encoderCaps.rate_controls.length > 0" :gutter="16">
          <!-- CRF / CQP / constqp / qscale（质量模式） -->
          <el-col v-if="isQualityMode(config.videoRateControl)" :span="8">
            <el-form-item :label="qualityModeLabel(config.videoRateControl)">
              <el-input-number v-model="config.videoCrf" :min="0" :max="config.videoRateControl==='qscale'?31:63" />
            </el-form-item>
          </el-col>
          <!-- 码率（所有需要目标码率的模式） -->
          <el-col v-if="needsBitrate(config.videoRateControl)" :span="needsMaxrate(config.videoRateControl) ? 8 : 12">
            <el-form-item label="目标码率 (bps)">
              <el-input v-model="config.videoBitrate" placeholder="如 2000000" />
            </el-form-item>
          </el-col>
          <!-- VBR-类：最大码率 + 缓冲 -->
          <template v-if="needsMaxrate(config.videoRateControl)">
            <el-col :span="8">
              <el-form-item label="最大码率 (bps)">
                <el-input v-model="config.videoMaxrate" placeholder="如 4000000" />
              </el-form-item>
            </el-col>
            <el-col :span="8">
              <el-form-item label="缓冲大小 (bits)">
                <el-input v-model="config.videoBufsize" placeholder="如 8000000" />
              </el-form-item>
            </el-col>
          </template>
          <!-- CBR 提示 -->
          <el-col v-if="config.videoRateControl === 'cbr' || config.videoRateControl === 'constqp'" :span="12">
            <span class="form-hint">恒定码率模式，码率严格限制</span>
          </el-col>
        </el-row>

        <!-- 预设 / Tune / Profile -->
        <el-row :gutter="16">
          <el-col v-if="encoderCaps.presets.length > 0" :span="8">
            <el-form-item label="编码预设">
              <el-select v-model="config.videoPreset" clearable placeholder="默认">
                <el-option v-for="p in encoderCaps.presets" :key="p" :label="p" :value="p" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col v-if="encoderCaps.tunes.length > 0" :span="7">
            <el-form-item label="Tune">
              <el-select v-model="config.videoTune" clearable placeholder="默认">
                <el-option v-for="t in encoderCaps.tunes" :key="t" :label="t" :value="t" />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col v-if="encoderCaps.profiles.length > 0" :span="9">
            <el-form-item label="Profile">
              <el-select v-model="config.videoProfile" clearable placeholder="默认">
                <el-option v-for="pf in encoderCaps.profiles" :key="pf" :label="pf" :value="pf" />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>

        <!-- 基础处理参数（编码器无关，始终可见） -->
        <el-divider content-position="left">基础处理</el-divider>
        <el-row :gutter="16">
          <el-col :span="8">
            <el-form-item label="缩放">
              <el-input v-model="config.videoScale" placeholder="如 1920x1080" />
            </el-form-item>
          </el-col>
          <el-col :span="8">
            <el-form-item label="帧率 (fps)">
              <el-input v-model="config.videoFps" placeholder="保持源帧率" />
            </el-form-item>
          </el-col>
          <el-col v-if="encoderCaps.pixel_fmts.length > 0" :span="8">
            <el-form-item label="像素格式">
              <el-select v-model="config.videoPixelFmt" clearable placeholder="默认">
                <el-option v-for="px in encoderCaps.pixel_fmts" :key="px" :label="px" :value="px" />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>
        <!-- 通用编码参数（general 分区，编码器无关但选择编码器后可见） -->
        <template v-if="config.videoCodec">
          <template v-for="sec in videoSections" :key="'g_'+sec.id">
            <template v-if="sec.id === 'general' && sec.params.length > 0">
              <el-row :gutter="16">
                <template v-for="p in sec.params" :key="p.name">
                  <el-col v-if="paramVisible(p)" :span="p.type === 'bool' ? 6 : 8">
                    <el-form-item :label="p.label">
                      <template v-if="p.type === 'select'">
                        <el-select v-model="encoderParamValues[p.name]" clearable placeholder="默认">
                          <el-option v-for="o in p.options" :key="o" :label="o" :value="o" />
                        </el-select>
                      </template>
                      <template v-else-if="p.type === 'bool'">
                        <el-switch v-model="encoderParamValues[p.name]" :active-value="1" :inactive-value="0" />
                      </template>
                      <template v-else-if="p.type === 'float'">
                        <el-input-number v-model="encoderParamValues[p.name]" :min="p.min" :max="p.max" :step="0.1" :precision="1" placeholder="默认" />
                      </template>
                      <template v-else>
                        <el-input-number v-model="encoderParamValues[p.name]" :min="p.min" :max="p.max" placeholder="默认" />
                      </template>
                    </el-form-item>
                  </el-col>
                </template>
              </el-row>
            </template>
          </template>
        </template>

        <!-- 高级选项（折叠）：线程数 + 编码器参数 -->
        <el-collapse v-if="config.videoCodec" class="advanced-collapse">
          <el-collapse-item title="高级选项" name="video_adv">
            <el-row :gutter="16">
              <el-col :span="8">
                <el-form-item label="线程数">
                  <el-input-number v-model="config.videoThreads" :min="0" placeholder="自动" />
                </el-form-item>
              </el-col>
            </el-row>
            <!-- 编码器专有参数 -->
            <el-row :gutter="16">
              <template v-for="p in nonGeneralVideoParams" :key="p.name">
                <el-col v-if="paramVisible(p)" :span="p.type === 'bool' ? 6 : 8">
                  <el-form-item :label="p.label">
                    <template v-if="p.type === 'select'">
                      <el-select v-model="encoderParamValues[p.name]" clearable placeholder="默认">
                        <el-option v-for="o in p.options" :key="o" :label="o" :value="o" />
                      </el-select>
                    </template>
                    <template v-else-if="p.type === 'bool'">
                      <el-switch v-model="encoderParamValues[p.name]" :active-value="1" :inactive-value="0" />
                    </template>
                    <template v-else-if="p.type === 'float'">
                      <el-input-number v-model="encoderParamValues[p.name]" :min="p.min" :max="p.max" :step="0.1" :precision="1" placeholder="默认" />
                    </template>
                    <template v-else>
                      <el-input-number v-model="encoderParamValues[p.name]" :min="p.min" :max="p.max" placeholder="默认" />
                    </template>
                  </el-form-item>
                </el-col>
              </template>
            </el-row>
          </el-collapse-item>
        </el-collapse>

        <!-- ============ 音频编码 ============ -->
        <el-divider content-position="left">音频编码</el-divider>
        <el-row :gutter="16">
          <el-col :span="12">
            <el-form-item label="编码器">
              <el-select v-model="config.audioCodec" clearable filterable placeholder="默认 = 流拷贝" @change="onAudioCodecChange">
                <el-option
                  v-for="c in audioCodecs"
                  :key="c.name"
                  :label="`${c.name} (${c.long_name})`"
                  :value="c.name"
                />
              </el-select>
            </el-form-item>
          </el-col>
          <!-- 音频码率控制选择器 -->
          <el-col v-if="audioCaps.rate_controls.length > 0" :span="6">
            <el-form-item label="码率控制">
              <el-select v-model="audioRateControl" @change="onAudioRateControlChange">
                <el-option v-for="rc in audioCaps.rate_controls" :key="rc" :label="audioRCLabel(rc)" :value="rc" />
              </el-select>
            </el-form-item>
          </el-col>
          <!-- VBR质量值 -->
          <el-col v-if="audioRateControl === 'vbr_quality'" :span="6">
            <el-form-item label="质量 (VBR)">
              <el-input-number v-model="config.audioQuality" :min="0" :max="9" placeholder="默认" />
            </el-form-item>
          </el-col>
          <!-- 码率 -->
          <el-col v-if="['cbr','abr','vbr','constrained_vbr'].includes(audioRateControl) || audioCaps.rate_controls.length === 0" :span="6">
            <el-form-item label="码率 (bps)">
              <el-input v-model="config.audioBitrate" placeholder="如 128000" />
            </el-form-item>
          </el-col>
        </el-row>
        <el-row v-if="config.audioCodec" :gutter="16">
          <el-col v-if="audioCaps.sample_rates.length > 0" :span="8">
            <el-form-item label="采样率 (Hz)">
              <el-select v-model="config.audioSampleRate" clearable placeholder="保持">
                <el-option
                  v-for="sr in audioCaps.sample_rates"
                  :key="sr"
                  :label="sr"
                  :value="sr"
                />
              </el-select>
            </el-form-item>
          </el-col>
          <el-col v-if="audioCaps.channel_layouts.length > 0" :span="8">
            <el-form-item label="声道布局">
              <el-select v-model="config.audioChannelLayout" clearable placeholder="保持">
                <el-option
                  v-for="cl in audioCaps.channel_layouts"
                  :key="cl"
                  :label="cl"
                  :value="cl"
                />
              </el-select>
            </el-form-item>
          </el-col>
        </el-row>

        <!-- 音频编码器参数 -->
        <template v-if="config.audioCodec && allAudioParams.length > 0">
          <el-row :gutter="16">
            <template v-for="p in allAudioParams" :key="p.name">
              <el-col :span="p.type === 'bool' ? 6 : 8">
                <el-form-item :label="p.label">
                  <template v-if="p.type === 'select'">
                    <el-select v-model="audioParamValues[p.name]" clearable placeholder="默认">
                      <el-option v-for="o in p.options" :key="o" :label="o" :value="o" />
                    </el-select>
                  </template>
                  <template v-else-if="p.type === 'bool'">
                    <el-switch v-model="audioParamValues[p.name]" :active-value="1" :inactive-value="0" />
                  </template>
                  <template v-else-if="p.type === 'float'">
                    <el-input-number v-model="audioParamValues[p.name]" :min="p.min" :max="p.max" :step="0.1" :precision="1" placeholder="默认" />
                  </template>
                  <template v-else>
                    <el-input-number v-model="audioParamValues[p.name]" :min="p.min" :max="p.max" placeholder="默认" />
                  </template>
                </el-form-item>
              </el-col>
            </template>
          </el-row>
        </template>

        <!-- ============ 图片编码 ============ -->
        <template v-if="imageCodecs.length > 0">
          <el-divider content-position="left">图片编码</el-divider>
          <el-row :gutter="16">
            <el-col :span="12">
              <el-form-item label="编码器">
                <el-select v-model="config.imageCodec" clearable filterable placeholder="默认 = 不编码图片" @change="onImageCodecChange">
                  <el-option
                    v-for="c in imageCodecs"
                    :key="c.name"
                    :label="`${c.name} (${c.long_name})`"
                    :value="c.name"
                  />
                </el-select>
              </el-form-item>
            </el-col>
          </el-row>
        </template>

        <!-- ============ 容器 ============ -->
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
          <el-button type="primary" @click="resetTask">开始新任务</el-button>
          <el-button @click="openOutputFolder">打开输出目录</el-button>
          <el-button @click="loadHistory">刷新历史</el-button>
        </template>
      </el-result>

      <div v-if="progress.result_files && progress.result_files.length > 0" class="result-files">
        <h4>输出文件 ({{ progress.result_files.length }})</h4>
        <div v-for="(f, i) in progress.result_files" :key="'out-'+i" class="result-item success-item">
          <el-icon><CircleCheckFilled /></el-icon>
          <span>{{ f }}</span>
        </div>
      </div>
      <div v-if="progress.result_errors && progress.result_errors.length > 0" class="result-errors">
        <h4>失败文件 ({{ progress.result_errors.length }})</h4>
        <div v-for="(f, i) in progress.result_errors" :key="'err-'+i" class="result-item error-item">
          <el-icon><CircleCloseFilled /></el-icon>
          <span>{{ f }}</span>
        </div>
      </div>
    </el-card>
  </div>
</template>

<script setup lang="ts">
import { ref, reactive, computed, onMounted } from 'vue'
import { ElMessage } from 'element-plus'
import { CircleCheckFilled, CircleCloseFilled } from '@element-plus/icons-vue'
import { uploadFiles, submitBatch, getCodecs, getHistory, openFolder, getEncoderParams, getAudioEncoderParams } from '../api'
import { useSSE, type SseEvent } from '../composables/useSSE'
import FileUploader, { type UploadFile } from '../components/FileUploader.vue'
import ProgressPanel, { type ProgressState } from '../components/ProgressPanel.vue'

// ---- 编码器能力（视频） ----
interface ParamDef {
  name: string
  label: string
  type: string         // "int" | "select" | "float" | "bool"
  min: number
  max: number
  default: number
  options?: string[]
  when_field?: string
  when_value?: string
  desc?: string
}

interface ParamSection {
  id: string
  label: string
  expanded: boolean
  params: ParamDef[]
}

interface EncoderCaps {
  rate_controls: string[]
  presets: string[]
  tunes: string[]
  profiles: string[]
  pixel_fmts: string[]
  param_sections: ParamSection[]
}

const encoderCaps = reactive<EncoderCaps>({
  rate_controls: [],
  presets: [],
  tunes: [],
  profiles: [],
  pixel_fmts: [],
  param_sections: [],
})

const defaultCaps: EncoderCaps = {
  rate_controls: [],
  presets: [],
  tunes: [],
  profiles: [],
  pixel_fmts: [],
  param_sections: [],
}

// 动态参数值
const encoderParamValues = reactive<Record<string, number | string>>({})

// 分区参数列表
const videoSections = computed(() => encoderCaps.param_sections)

// 所有参数扁平列表（用于取值遍历）
const videoParams = computed(() => encoderCaps.param_sections.flatMap(s => s.params))

// 非通用参数扁平列表（codec + advanced 分区，用于高级选项）
const nonGeneralVideoParams = computed(() =>
  encoderCaps.param_sections
    .filter(s => s.id !== 'general')
    .flatMap(s => s.params)
)

// 判断参数是否应在当前条件下显示
function paramVisible(p: ParamDef): boolean {
  if (!p.when_field) return true
  if (p.when_field === 'rate_control') {
    return config.videoRateControl === p.when_value
  }
  // 通用条件：检查另一个参数的值
  const targetVal = encoderParamValues[p.when_field] ?? audioParamValues[p.when_field]
  if (p.when_value === '>0') {
    return Number(targetVal) > 0
  }
  return String(targetVal) === p.when_value
}

function rateControlLabel(rc: string): string {
  const map: Record<string, string> = {
    crf: 'CRF (质量优先)',
    cqp: 'CQP (固定量化)',
    constqp: 'CQP (固定量化)',
    abr: 'ABR (平均码率)',
    cbr: 'CBR (恒定码率)',
    cbr_hq: 'CBR HQ (高质量恒定码率)',
    cbr_ld_hq: 'CBR LD HQ (低延迟高质量)',
    vbr: 'VBR (可变码率)',
    vbr_hq: 'VBR HQ (高质量可变码率)',
    vbr_peak: 'VBR Peak (峰值约束)',
    vbr_latency: 'VBR Latency (低延迟)',
    qvbr: 'QVBR (质量可变)',
    hqvbr: 'HQVBR (高质量可变)',
    hqcbr: 'HQCBR (高质量恒定)',
    avbr: 'AVBR (自适应码率)',
    icq: 'ICQ (智能恒定质量)',
    la: 'LA (前瞻VBR)',
    qscale: 'QScale (质量缩放)',
  }
  return map[rc] || rc.toUpperCase()
}

async function onVideoCodecChange(codec: string) {
  if (!codec) {
    Object.assign(encoderCaps, defaultCaps)
    config.videoRateControl = ''
    // 清空动态参数值
    Object.keys(encoderParamValues).forEach(k => delete encoderParamValues[k])
    return
  }
  try {
    const resp = await getEncoderParams(codec)
    if (resp.data) {
      Object.assign(encoderCaps, resp.data)
      // 默认选中第一个码率控制模式
      if (encoderCaps.rate_controls.length > 0) {
        config.videoRateControl = encoderCaps.rate_controls[0]
      }
      // 初始化动态参数默认值
      Object.keys(encoderParamValues).forEach(k => delete encoderParamValues[k])
      for (const sec of encoderCaps.param_sections) {
        for (const p of sec.params) {
          if (p.type === 'bool') {
            encoderParamValues[p.name] = (p.default === 1) ? 1 : 0
          }
        }
      }
    }
  } catch {
    Object.assign(encoderCaps, defaultCaps)
    config.videoRateControl = ''
    Object.keys(encoderParamValues).forEach(k => delete encoderParamValues[k])
  }
}

function onRateControlChange(_rc: string) {
  // 切换模式时清空相关字段避免混淆
  config.videoCrf = 23
  config.videoBitrate = ''
  config.videoMaxrate = ''
  config.videoBufsize = ''
}

// 质量模式：CRF / CQP / constqp / qscale / icq
function isQualityMode(rc: string): boolean {
  return ['crf','cqp','constqp','qscale'].includes(rc)
}
function qualityModeLabel(rc: string): string {
  const map: Record<string, string> = { crf:'CRF 质量', cqp:'CQP 值', constqp:'CQP 值', qscale:'QScale 值' }
  return map[rc] || '质量值'
}
// 需要目标码率的模式
function needsBitrate(rc: string): boolean {
  const modes = ['abr','cbr','vbr','vbr_hq','vbr_peak','vbr_latency','cbr_hq','cbr_ld_hq','hqcbr','hqvbr','qvbr','avbr','la']
  return modes.includes(rc)
}
// VBR-类需要峰值约束
function needsMaxrate(rc: string): boolean {
  return ['vbr','vbr_hq','vbr_peak','vbr_latency','hqvbr','la'].includes(rc)
}

// ---- 编码器能力（音频） ----
interface AudioEncoderCaps {
  sample_rates: number[]
  channel_layouts: string[]
  has_quality: boolean
  has_bitrate: boolean
  rate_controls: string[]
  param_sections: ParamSection[]
}

const audioCaps = reactive<AudioEncoderCaps>({
  sample_rates: [],
  channel_layouts: [],
  has_quality: false,
  has_bitrate: false,
  rate_controls: [],
  param_sections: [],
})

const defaultAudioCaps: AudioEncoderCaps = {
  sample_rates: [],
  channel_layouts: [],
  has_quality: false,
  has_bitrate: false,
  rate_controls: [],
  param_sections: [],
}

// 音频码率控制
const audioRateControl = ref('')

function onAudioRateControlChange(rc: string) {
  audioRateControl.value = rc
}

function audioRCLabel(rc: string): string {
  const map: Record<string, string> = {
    cbr: 'CBR (恒定码率)',
    abr: 'ABR (平均码率)',
    vbr: 'VBR (可变码率)',
    vbr_quality: 'VBR (质量优先)',
    constrained_vbr: '约束VBR',
    lossless: '无损',
  }
  return map[rc] || rc.toUpperCase()
}

async function onImageCodecChange(_codec: string) {
  // 图片编码器：加载参数（可复用视频 encoder-params API）
  if (!_codec) return
  try {
    const resp = await getEncoderParams(_codec)
    if (resp.data) {
      // 图片编码器暂时只使用码率控制面板，不覆盖视频 encoderCaps
    }
  } catch { /* ignore */ }
}

const audioParamValues = reactive<Record<string, number | string>>({})

const audioSections = computed(() => audioCaps.param_sections)

const audioParams = computed(() => audioCaps.param_sections.flatMap(s => s.params))

const allAudioParams = computed(() => audioCaps.param_sections.flatMap(s => s.params))

async function onAudioCodecChange(codec: string) {
  if (!codec) {
    Object.assign(audioCaps, defaultAudioCaps)
    Object.keys(audioParamValues).forEach(k => delete audioParamValues[k])
    return
  }
  try {
    const resp = await getAudioEncoderParams(codec)
    if (resp.data) {
      Object.assign(audioCaps, resp.data)
      // 默认选择第一个码率控制模式
      audioRateControl.value = audioCaps.rate_controls.length > 0 ? audioCaps.rate_controls[0] : ''
      // 初始化动态参数默认值
      Object.keys(audioParamValues).forEach(k => delete audioParamValues[k])
      for (const sec of audioCaps.param_sections) {
        for (const p of sec.params) {
          if (p.type === 'bool') {
            audioParamValues[p.name] = (p.default === 1) ? 1 : 0
          }
        }
      }
    }
  } catch {
    Object.assign(audioCaps, defaultAudioCaps)
    Object.keys(audioParamValues).forEach(k => delete audioParamValues[k])
  }
}

// ---- 文件 ----
const selectedFiles = ref<UploadFile[]>([])

// ---- 配置 ----
const config = reactive({
  outputDir: './data/output',
  structure: 'by_type',
  // 视频
  videoCodec: '',
  videoRateControl: '',
  videoCrf: 23,
  videoBitrate: '',
  videoMaxrate: '',
  videoBufsize: '',
  videoPreset: '',
  videoTune: '',
  videoProfile: '',
  videoScale: '',
  videoFps: '',
  videoPixelFmt: '',
  videoGop: 0,
  videoThreads: 0,
  // 音频
  audioCodec: '',
  audioBitrate: '',
  audioSampleRate: '' as string | number,
  audioChannelLayout: '',
  audioQuality: 0,
  // 容器
  format: '',
  overwrite: false,
  // 图片
  imageCodec: '',
})

// ---- 可用编解码器 ----
const videoCodecs = ref<{ name: string; long_name: string; is_hardware: boolean; is_image: boolean }[]>([])
const audioCodecs = ref<{ name: string; long_name: string; is_hardware: boolean; is_image: boolean }[]>([])
const imageCodecs = ref<{ name: string; long_name: string; is_hardware: boolean; is_image: boolean }[]>([])

onMounted(async () => {
  try {
    const [vResp, aResp] = await Promise.all([
      getCodecs('video'),
      getCodecs('audio'),
    ])
    const allVideo = vResp.data.codecs || []
    videoCodecs.value = allVideo.filter((c: any) => !c.is_image)
    imageCodecs.value = allVideo.filter((c: any) => c.is_image)
    audioCodecs.value = aResp.data.codecs || []
  } catch { /* fallback */ }

  loadHistory()
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
  loadHistory()
})

// ---- 处理历史 ----
const historyOpen = ref<string[]>([])
const history = ref<any[]>([])

async function loadHistory() {
  try {
    const resp = await getHistory()
    history.value = (resp.data || []).reverse()
  } catch { /* ignore */ }
}

function formatTime(ts: number): string {
  if (!ts) return ''
  const d = new Date(ts * 1000)
  return d.toLocaleString('zh-CN')
}

function openHistoryFolder(item: any) {
  if (item.result_files && item.result_files[0]) {
    openFolder(item.result_files[0].replace(/\/[^\/]+$/, ''))
  }
}

async function openOutputFolder() {
  try {
    await openFolder(config.outputDir || './data/output')
    ElMessage.success('已打开输出目录')
  } catch {
    ElMessage.error('无法打开目录')
  }
}

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
        dir: config.outputDir || './data/output',
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

        // 码率控制 → 映射到 crf / bitrate / maxrate / bufsize
        const rc = config.videoRateControl
        if (rc === 'crf' || rc === 'cqp') {
          if (config.videoCrf >= 0) video.crf = config.videoCrf
        }
        if (rc === 'abr' || rc === 'cbr' || rc === 'vbr') {
          if (config.videoBitrate) video.bitrate = parseInt(config.videoBitrate)
        }
        if (rc === 'cbr') {
          // CBR: maxrate = bitrate, bufsize = bitrate * 2
          if (config.videoBitrate) {
            video.maxrate = parseInt(config.videoBitrate)
            video.bufsize = parseInt(config.videoBitrate) * 2
          }
        }
        if (rc === 'vbr') {
          if (config.videoMaxrate) video.maxrate = parseInt(config.videoMaxrate)
          if (config.videoBufsize) video.bufsize = parseInt(config.videoBufsize)
        }

        if (config.videoPreset) video.preset = config.videoPreset
        if (config.videoTune) video.tune = config.videoTune
        if (config.videoProfile) video.profile = config.videoProfile
        if (config.videoScale) {
          const [w, h] = config.videoScale.split('x').map(Number)
          if (w) video.width = w
          if (h) video.height = h
        }
        if (config.videoFps) video.fps = parseFloat(config.videoFps)
        if (config.videoPixelFmt) video.pixel_fmt = config.videoPixelFmt
        if (config.videoGop > 0) video.gop_size = config.videoGop
        if (config.videoThreads > 0) video.threads = config.videoThreads

        // 编码器专有参数 → opts JSON（根据 param 定义的类型转换）
        const videoOpts: Record<string, any> = {}
        for (const [key, val] of Object.entries(encoderParamValues)) {
          if (val !== '' && val !== undefined && val !== -1) {
            const paramDef = videoParams.value.find(p => p.name === key)
            if (paramDef?.type === 'bool') {
              videoOpts[key] = val === 1 || val === '1'
            } else if (typeof val === 'string') {
              videoOpts[key] = val
            } else {
              videoOpts[key] = Number(val)
            }
          }
        }
        if (Object.keys(videoOpts).length > 0) video.opts = videoOpts

        if (Object.keys(video).length > 0) job.video = video

        // 音频配置
        const audio: any = {}
        if (config.audioCodec) audio.codec = config.audioCodec
        if (config.audioBitrate) audio.bitrate = parseInt(config.audioBitrate)
        if (config.audioSampleRate) audio.sample_rate = Number(config.audioSampleRate)
        if (config.audioChannelLayout) audio.channel_layout = config.audioChannelLayout

        // 音频编码器专有参数 → opts JSON（根据 param 定义的类型转换）
        const audioOpts: Record<string, any> = {}
        for (const [key, val] of Object.entries(audioParamValues)) {
          if (val !== '' && val !== undefined && val !== -1) {
            const paramDef = audioParams.value.find(p => p.name === key)
            if (paramDef?.type === 'bool') {
              audioOpts[key] = val === 1 || val === '1'
            } else if (typeof val === 'string') {
              audioOpts[key] = val
            } else if (typeof val === 'boolean') {
              audioOpts[key] = val
            } else {
              audioOpts[key] = Number(val)
            }
          }
        }
        if (Object.keys(audioOpts).length > 0) audio.opts = audioOpts

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
    result_files: [],
    result_errors: [],
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

.history-panel {
  margin-bottom: 16px;
  background: #fafafa;
  border-radius: 8px;
}

.history-list {
  max-height: 260px;
  overflow-y: auto;
}

.history-entry {
  padding: 10px 12px;
  border-bottom: 1px solid #ebeef5;
  font-size: 13px;
}

.history-entry:last-child {
  border-bottom: none;
}

.hist-success {
  border-left: 3px solid #67c23a;
}

.hist-fail {
  border-left: 3px solid #f56c6c;
}

.hist-meta {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.hist-time {
  color: #909399;
  font-size: 12px;
}

.hist-count {
  color: #606266;
}

.hist-files {
  margin-top: 6px;
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.hist-file {
  background: #ecf5ff;
  color: #409eff;
  padding: 2px 8px;
  border-radius: 3px;
  font-size: 12px;
  word-break: break-all;
}

.config-card {
  margin-top: 16px;
}

.advanced-collapse {
  margin-top: 8px;
  border: none;
}

.advanced-collapse :deep(.el-collapse-item__header) {
  font-size: 13px;
  color: #909399;
  border: none;
}

.advanced-collapse :deep(.el-collapse-item__wrap) {
  border: none;
}

.form-hint {
  font-size: 12px;
  color: #909399;
  line-height: 32px;
}

.section-label {
  font-size: 13px;
  font-weight: 500;
  color: #606266;
  margin-bottom: 4px;
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

.result-files h4,
.result-errors h4 {
  font-size: 14px;
  margin: 16px 0 8px;
  color: #303133;
}

.result-item {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 10px;
  border-radius: 4px;
  font-size: 13px;
  margin-bottom: 4px;
  word-break: break-all;
}

.success-item {
  background: #f0f9eb;
  color: #67c23a;
}

.error-item {
  background: #fef0f0;
  color: #f56c6c;
}
</style>
