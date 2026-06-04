<template>
  <el-card v-if="visible" class="progress-panel" shadow="never">
    <template #header>
      <div class="panel-header">
        <span>处理进度</span>
        <el-tag v-if="isRunning" type="warning" size="small">处理中</el-tag>
        <el-tag v-else-if="isDone" type="success" size="small">已完成</el-tag>
        <el-tag v-else type="info" size="small">等待中</el-tag>
      </div>
    </template>

    <!-- 总体进度条 -->
    <div class="progress-section">
      <div class="progress-info">
        <span>
          任务 {{ progress.current_job + 1 }} / {{ progress.total_jobs }}
        </span>
        <span>
          成功 {{ progress.ok_count }} | 失败 {{ progress.fail_count }}
        </span>
      </div>
      <el-progress
        :percentage="percent"
        :status="isFailed ? 'exception' : undefined"
        :stroke-width="16"
      />
    </div>

    <!-- 当前文件 -->
    <div v-if="progress.current_file" class="current-file">
      <el-icon><Document /></el-icon>
      <span class="file-name">{{ progress.current_file }}</span>
      <el-tag
        :type="statusTagType"
        size="small"
        class="status-tag"
      >
        {{ statusLabel }}
      </el-tag>
    </div>

    <!-- 错误信息 -->
    <el-alert
      v-if="progress.error"
      :title="progress.error"
      type="error"
      show-icon
      :closable="false"
      style="margin-top: 12px"
    />
  </el-card>
</template>

<script setup lang="ts">
import { computed } from 'vue'

export interface ProgressState {
  status: string
  current_job: number
  total_jobs: number
  current_file: string
  job_status: string
  ok_count: number
  fail_count: number
  error?: string
}

const props = defineProps<{
  visible: boolean
  progress: ProgressState
}>()

const percent = computed(() => {
  if (props.progress.total_jobs === 0) return 0
  return Math.round(
    (props.progress.current_job / props.progress.total_jobs) * 100
  )
})

const isRunning = computed(
  () => props.progress.status === 'running'
)
const isDone = computed(
  () => props.progress.status === 'completed' || props.progress.status === 'failed'
)
const isFailed = computed(
  () => props.progress.status === 'failed'
)

const statusTagType = computed(() => {
  switch (props.progress.job_status) {
    case 'processing': return 'warning'
    case 'ok': return 'success'
    case 'fail': return 'danger'
    default: return 'info'
  }
})

const statusLabel = computed(() => {
  switch (props.progress.job_status) {
    case 'processing': return '转码中'
    case 'ok': return '成功'
    case 'fail': return '失败'
    default: return '等待'
  }
})
</script>

<style scoped>
.panel-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.progress-section {
  margin-bottom: 16px;
}

.progress-info {
  display: flex;
  justify-content: space-between;
  font-size: 13px;
  color: #606266;
  margin-bottom: 8px;
}

.current-file {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 8px 12px;
  background: #f5f7fa;
  border-radius: 4px;
  font-size: 13px;
}

.file-name {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.status-tag {
  flex-shrink: 0;
}
</style>
