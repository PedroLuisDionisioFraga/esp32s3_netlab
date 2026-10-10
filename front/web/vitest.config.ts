import { defineConfig } from 'vitest/config'

// Only the pure modules (chart maths, history, memory model) are tested: no DOM, and none of the build steps of
// vite.config.ts (gzip) have any business running under the test runner.
export default defineConfig({
  test: {
    environment: 'node',
    include: ['src/**/*.test.ts'],
  },
})
