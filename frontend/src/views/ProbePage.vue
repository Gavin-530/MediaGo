<template>
  <div class="probe-page">
    <h1 class="page-title">媒体探测</h1>
    <p class="page-desc">查看媒体文件的完整属性信息</p>

    <el-card shadow="never">
      <el-form inline size="default">
        <el-form-item label="文件路径">
          <el-input
            v-model="filePath"
            placeholder="请输入文件完整路径，如 D:\Videos\sample.mp4"
            style="width: 480px"
            clearable
            @keyup.enter="doProbe"
          />
        </el-form-item>
        <el-form-item>
          <el-button type="primary" :loading="probing" @click="doProbe">
            <el-icon><Search /></el-icon>
            探测
          </el-button>
        </el-form-item>
      </el-form>
    </el-card>

    <!-- 错误 -->
    <el-alert
      v-if="error"
      :title="error"
      type="error"
      show-icon
      :closable="false"
      style="margin-top: 16px"
    />

    <!-- 载入中 -->
    <el-card v-if="probing" shadow="never" style="margin-top: 16px">
      <el-skeleton :rows="6" animated />
    </el-card>

    <!-- 结果 -->
    <ProbeResult
      v-if="result && !probing"
      :visible="true"
      :info="result"
    />
  </div>
</template>

<script setup lang="ts">
import { ref } from 'vue'
import { probeFile } from '../api'
import ProbeResult, { type ProbeInfo } from '../components/ProbeResult.vue'

const filePath = ref('')
const probing = ref(false)
const error = ref('')
const result = ref<ProbeInfo | null>(null)

async function doProbe() {
  if (!filePath.value.trim()) return

  probing.value = true
  error.value = ''
  result.value = null

  try {
    const resp = await probeFile(filePath.value.trim())
    result.value = resp.data
  } catch (err: any) {
    error.value = err.response?.data?.error || '探测失败'
  } finally {
    probing.value = false
  }
}
</script>

<style scoped>
.probe-page {
  max-width: 800px;
  margin: 0 auto;
}

.page-title {
  font-size: 22px;
  font-weight: 600;
  margin-bottom: 4px;
  color: #1a1a2e;
}

.page-desc {
  color: #909399;
  font-size: 14px;
  margin-bottom: 20px;
}
</style>
