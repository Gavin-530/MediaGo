import axios from 'axios'

const api = axios.create({
  baseURL: '/api',
  timeout: 30000,
})

// ---- 健康检查 ----
export function healthCheck() {
  return api.get('/health')
}

// ---- 文件上传 ----
export function uploadFiles(formData: FormData) {
  return api.post('/upload', formData, {
    headers: { 'Content-Type': 'multipart/form-data' },
  })
}

// ---- 批量转码 ----
export function submitBatch(manifest: object) {
  return api.post('/batch', manifest)
}

// ---- SSE 进度 ----
export function getProgressUrl(taskId: string): string {
  return `/api/progress/${taskId}`
}

// ---- 媒体探测 ----
export function probeFile(filePath: string) {
  return api.get('/probe', { params: { path: filePath } })
}

// ---- 编解码器列表 ----
export function getCodecs(type?: 'video' | 'audio') {
  return api.get('/codecs', { params: type ? { type } : {} })
}

// ---- 像素格式列表 ----
export function getPixFmts() {
  return api.get('/pixfmts')
}

// ---- 处理历史 ----
export function getHistory() {
  return api.get('/history')
}

// ---- 打开目录 ----
export function openFolder(dir: string) {
  return api.post('/open-folder', { dir })
}

// ---- 编码器参数 ----
export function getEncoderParams(codec: string) {
  return api.post('/encoder-params', { codec })
}

// ---- 音频编码器参数 ----
export function getAudioEncoderParams(codec: string) {
  return api.post('/audio-encoder-params', { codec })
}

export default api
