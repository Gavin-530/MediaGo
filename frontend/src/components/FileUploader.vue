<template>
  <el-card class="uploader" shadow="never">
    <template #header>
      <div class="uploader-header">
        <span>选择文件</span>
        <el-upload
          multiple
          :auto-upload="false"
          :show-file-list="false"
          :on-change="handleChange"
          accept="*"
        >
          <el-button type="primary" size="small">
            <el-icon><Plus /></el-icon>
            添加文件
          </el-button>
        </el-upload>
      </div>
    </template>

    <!-- 拖拽上传区域 -->
    <div
      class="drop-zone"
      :class="{ 'is-dragover': isDragOver }"
      @dragover.prevent="isDragOver = true"
      @dragleave.prevent="isDragOver = false"
      @drop.prevent="handleDrop"
    >
      <el-icon :size="36" color="#c0c4cc"><UploadFilled /></el-icon>
      <p v-if="files.length === 0">
        拖拽文件到此处，或点击上方「添加文件」
      </p>
      <p v-else>继续拖拽添加更多文件</p>
    </div>

    <!-- 已选文件列表 -->
    <div v-if="files.length > 0" class="file-list">
      <el-tag
        v-for="(file, idx) in files"
        :key="idx"
        closable
        :type="fileType(file.name)"
        class="file-tag"
        @close="removeFile(idx)"
      >
        <el-icon class="tag-icon"><Document /></el-icon>
        {{ file.name }}
      </el-tag>
    </div>
  </el-card>
</template>

<script setup lang="ts">
import { ref } from 'vue'

export interface UploadFile {
  name: string
  raw: File
}

const props = defineProps<{
  modelValue: UploadFile[]
}>()

const emit = defineEmits<{
  'update:modelValue': [files: UploadFile[]]
}>()

const isDragOver = ref(false)

const files = ref<UploadFile[]>([...props.modelValue])

function fileType(name: string): string {
  const ext = name.split('.').pop()?.toLowerCase() || ''
  if (['mp4', 'mkv', 'avi', 'mov', 'webm', 'flv'].includes(ext)) return 'success'
  if (['jpg', 'jpeg', 'png', 'bmp', 'webp', 'gif', 'svg'].includes(ext)) return 'warning'
  if (['mp3', 'aac', 'wav', 'flac', 'ogg'].includes(ext)) return ''
  return 'info'
}

function addRawFiles(rawList: FileList | File[]) {
  const newFiles: UploadFile[] = []
  for (let i = 0; i < rawList.length; i++) {
    const f = rawList[i]
    // 防止重复
    if (!files.value.find((x) => x.name === f.name && x.raw.size === f.size)) {
      newFiles.push({ name: f.name, raw: f })
    }
  }
  files.value.push(...newFiles)
  emit('update:modelValue', [...files.value])
}

function handleChange(_file: any, fileList: any) {
  const raws = fileList.map((f: any) => f.raw).filter(Boolean)
  addRawFiles(raws)
}

function handleDrop(e: DragEvent) {
  isDragOver.value = false
  if (e.dataTransfer?.files) {
    addRawFiles(e.dataTransfer.files)
  }
}

function removeFile(idx: number) {
  files.value.splice(idx, 1)
  emit('update:modelValue', [...files.value])
}
</script>

<style scoped>
.uploader-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.drop-zone {
  border: 2px dashed #dcdfe6;
  border-radius: 6px;
  padding: 32px;
  text-align: center;
  transition: border-color 0.3s, background 0.3s;
  cursor: pointer;
}

.drop-zone.is-dragover {
  border-color: #409eff;
  background: #ecf5ff;
}

.drop-zone p {
  color: #909399;
  font-size: 14px;
  margin-top: 8px;
}

.file-list {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 12px;
}

.file-tag {
  display: flex;
  align-items: center;
  gap: 4px;
}

.tag-icon {
  margin-right: 2px;
}
</style>
