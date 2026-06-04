<template>
  <el-card v-if="visible" class="probe-result" shadow="never">
    <template #header>
      <span>探测结果</span>
    </template>

    <el-descriptions :column="2" border size="small">
      <el-descriptions-item label="编解码器">
        {{ info.codec_name }}
      </el-descriptions-item>
      <el-descriptions-item label="容器格式">
        {{ info.container }}
      </el-descriptions-item>
      <el-descriptions-item label="分辨率">
        {{ info.width }} × {{ info.height }}
      </el-descriptions-item>
      <el-descriptions-item label="像素格式">
        {{ info.pix_fmt_name }}
      </el-descriptions-item>
      <el-descriptions-item label="位深">
        {{ info.bit_depth }} bit
      </el-descriptions-item>
      <el-descriptions-item label="色彩空间">
        {{ colorSpaceLabel }}
      </el-descriptions-item>
      <el-descriptions-item label="色彩范围">
        {{ colorRangeLabel }}
      </el-descriptions-item>
      <el-descriptions-item label="流数量">
        {{ info.nb_streams }}
      </el-descriptions-item>
      <el-descriptions-item label="媒体类型">
        <el-tag :type="info.is_image ? 'warning' : 'success'" size="small">
          {{ info.is_image ? '图像' : '视频/音频' }}
        </el-tag>
      </el-descriptions-item>
      <el-descriptions-item label="透明通道">
        <el-tag :type="info.has_alpha ? 'success' : 'info'" size="small">
          {{ info.has_alpha ? '有' : '无' }}
        </el-tag>
      </el-descriptions-item>
      <el-descriptions-item label="ICC Profile">
        <el-tag :type="info.has_icc ? 'success' : 'info'" size="small">
          {{ info.has_icc ? '有' : '无' }}
        </el-tag>
      </el-descriptions-item>
    </el-descriptions>
  </el-card>
</template>

<script setup lang="ts">
import { computed } from 'vue'

export interface ProbeInfo {
  codec_name: string
  codec_id: number
  width: number
  height: number
  pix_fmt: number
  pix_fmt_name: string
  bit_depth: number
  color_space: number
  color_range: number
  container: string
  has_icc: boolean
  has_alpha: boolean
  nb_streams: number
  is_image: boolean
}

const props = defineProps<{
  visible: boolean
  info: ProbeInfo
}>()

// FFmpeg color space constants
const colorSpaceMap: Record<number, string> = {
  0: 'RGB',
  1: 'BT.709',
  2: '未指定',
  4: 'FCC',
  5: 'BT.470BG',
  6: 'SMPTE 170M',
  7: 'SMPTE 240M',
  8: 'YCGCO',
  9: 'BT.2020 NCL',
  10: 'BT.2020 CL',
}

const colorRangeMap: Record<number, string> = {
  0: '未指定',
  1: 'MPEG (Limited)',
  2: 'JPEG (Full)',
}

const colorSpaceLabel = computed(() => {
  return colorSpaceMap[props.info.color_space] || '未知'
})

const colorRangeLabel = computed(() => {
  return colorRangeMap[props.info.color_range] || '未知'
})
</script>
