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
}

export function useSSE(onMessage: (event: SseEvent) => void, onDone?: () => void) {
  const connected = ref(false)
  let eventSource: EventSource | null = null

  function connect(taskId: string) {
    disconnect()
    const url = `/api/progress/${taskId}`
    eventSource = new EventSource(url)

    eventSource.onopen = () => {
      connected.value = true
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
      disconnect()
      onDone?.()
    }
  }

  function disconnect() {
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
