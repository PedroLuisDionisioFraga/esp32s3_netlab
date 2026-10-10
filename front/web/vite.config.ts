import { readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { extname, join } from 'node:path'
import { gzipSync } from 'node:zlib'
import vue from '@vitejs/plugin-vue'
import { defineConfig, loadEnv, type Plugin } from 'vite'

// Headers the device's small HTTP server does not need. Dropping them keeps the dev
// proxy's requests well under its request-header limit (cookies from localhost are the usual culprit).
// "authorization" is NOT here: the session token has to reach the device.
const DROPPED_HEADERS = ['cookie', 'referer', 'origin', 'user-agent', 'accept-language', 'accept-encoding']

// Files worth compressing. Fonts are woff2 already, and index.html is read first and often, so those stay as they are.
const GZIP_EXTENSIONS = new Set(['.js', '.mjs', '.css', '.svg', '.json', '.ico', '.txt', '.webmanifest'])

/**
 * After the build, replaces every compressible file by its gzip (`app.js` -> `app.js.gz`). The device serves the
 * `.gz` with `Content-Encoding: gzip` (components/rest_server/rest_server.c): a third of the bytes to push through a
 * server that answers one request at a time, and less flash used.
 */
function gzipDist(): Plugin {
  let outDir = 'dist'
  return {
    name: 'netlab-gzip-dist',
    apply: 'build',
    configResolved(config) {
      outDir = config.build.outDir
    },
    closeBundle() {
      const walk = (dir: string): string[] =>
        readdirSync(dir, { withFileTypes: true }).flatMap((entry) =>
          entry.isDirectory() ? walk(join(dir, entry.name)) : [join(dir, entry.name)],
        )
      for (const file of walk(outDir)) {
        if (!GZIP_EXTENSIONS.has(extname(file))) continue
        writeFileSync(`${file}.gz`, gzipSync(readFileSync(file), { level: 9 }))
        rmSync(file)
      }
    },
  }
}

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const espHost = env.ESP_HOST || 'http://netlab.local'

  return {
    plugins: [vue(), gzipDist()],
    build: {
      outDir: 'dist',
      emptyOutDir: true,
      // Fonts and icons stay files: the device caches them forever under /assets, and a data: URI cannot be.
      assetsInlineLimit: 0,
      rollupOptions: {
        output: {
          // The www partition is LittleFS: a name (plus ".gz") must stay under 64 characters.
          chunkFileNames: 'assets/[name]-[hash:8].js',
          entryFileNames: 'assets/[name]-[hash:8].js',
          assetFileNames: 'assets/[name]-[hash:8][extname]',
        },
      },
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
