import { ref, onUnmounted } from 'vue'

export interface SseEvent {
  status: string
  current_job: number
  total_jobs: number
  current_file: string
  job_status: string
  ok_count: number
  fail_count: number
  error?: string
  result_files?: string[]
  result_errors?: string[]
}

export function useSSE(onMessage: (event: SseEvent) => void, onDone?: () => void) {
  const connected = ref(false)
  let eventSource: EventSource | null = null
  let retryTimer: ReturnType<typeof setTimeout> | null = null
  let retryCount = 0
  const MAX_RETRIES = 5
  let currentTaskId = ''

  function connect(taskId: string) {
    disconnect()
    currentTaskId = taskId
    retryCount = 0
    doConnect()
  }

  function doConnect() {
    if (!currentTaskId) return

    // 绕过 Vite 代理直连后端，避免代理缓冲 SSE 流导致进度不推送
    const baseUrl = import.meta.env.DEV
      ? 'http://127.0.0.1:9527'
      : ''
    const url = `${baseUrl}/api/progress/${currentTaskId}`
    eventSource = new EventSource(url)

    eventSource.onopen = () => {
      connected.value = true
      retryCount = 0
    }

    eventSource.onmessage = (event) => {
      try {
        const data: SseEvent = JSON.parse(event.data)
        onMessage(data)
      } catch {
        // ignore parse errors
      }
    }

    eventSource.addEventListener('done', () => {
      disconnect()
      onDone?.()
    })

    eventSource.onerror = () => {
      eventSource?.close()
      eventSource = null
      connected.value = false

      // 服务器发送 done 后连接关闭属于正常终止，不重连
      // 网络错误则重试
      if (retryCount < MAX_RETRIES && currentTaskId) {
        retryCount++
        const delay = Math.min(1000 * Math.pow(2, retryCount), 10000)
        retryTimer = setTimeout(doConnect, delay)
      } else if (retryCount >= MAX_RETRIES) {
        onDone?.()
      }
    }
  }

  function disconnect() {
    currentTaskId = ''
    if (retryTimer) {
      clearTimeout(retryTimer)
      retryTimer = null
    }
    if (eventSource) {
      eventSource.close()
      eventSource = null
    }
    connected.value = false
  }

  onUnmounted(() => {
    disconnect()
  })

  return { connected, connect, disconnect }
}
