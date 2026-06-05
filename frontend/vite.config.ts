import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  server: {
    port: 5173,
    // 开发时代理 API 请求到 C++ 后端
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:9527',
        changeOrigin: true,
        // SSE 需要禁用缓冲，否则事件会被 proxy 积压
        configure: (proxy) => {
          proxy.on('proxyReq', (_proxyReq, req) => {
            // SSE 请求：禁用超时与缓冲
            if (req.headers.accept?.includes('text/event-stream')) {
              req.socket?.setTimeout?.(0)
              req.socket?.setNoDelay?.(true)
              req.socket?.setKeepAlive?.(true)
            }
          })
          proxy.on('proxyRes', (proxyRes, req) => {
            if (req.headers.accept?.includes('text/event-stream')) {
              // 强制关闭缓冲
              proxyRes.headers['cache-control'] = 'no-cache'
              proxyRes.headers['x-accel-buffering'] = 'no'
            }
          })
        },
      },
    },
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
})
