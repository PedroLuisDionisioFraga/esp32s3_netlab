import vue from '@vitejs/plugin-vue'
import { defineConfig, loadEnv } from 'vite'

// Headers the device's small HTTP server does not need. Dropping them keeps the dev
// proxy's requests well under its request-header limit (cookies from localhost are the usual culprit).
const DROPPED_HEADERS = ['cookie', 'referer', 'origin', 'user-agent', 'accept-language', 'accept-encoding']

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const espHost = env.ESP_HOST || 'http://netlab.local'

  return {
    plugins: [vue()],
    build: {
      outDir: 'dist',
      emptyOutDir: true,
    },
    server: {
      port: 5173,
      proxy: {
        // `pnpm dev` serves the UI from the PC and forwards /api to the device.
        '/api': {
          target: espHost,
          changeOrigin: true,
          configure: (proxy) => {
            proxy.on('proxyReq', (proxyReq) => {
              for (const name of Object.keys(proxyReq.getHeaders())) {
                if (DROPPED_HEADERS.includes(name) || name.startsWith('sec-')) {
                  proxyReq.removeHeader(name)
                }
              }
            })
          },
        },
      },
    },
  }
})
