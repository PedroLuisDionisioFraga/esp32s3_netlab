import { onMounted, onUnmounted, ref } from 'vue'

// Calls `fetcher` every `intervalMs`. It never overlaps two requests and pauses while the
// tab is hidden, so an open tab does not keep hammering a small device.
export function usePolling(fetcher, intervalMs) {
  const data = ref(null)
  const error = ref(null)
  let timer = null
  let inFlight = false

  async function refresh() {
    if (inFlight || document.hidden) {
      return
    }
    inFlight = true
    try {
      data.value = await fetcher()
      error.value = null
    } catch (err) {
      error.value = err.message ?? String(err)
    } finally {
      inFlight = false
    }
  }

  function onVisibilityChange() {
    if (!document.hidden) {
      refresh()
    }
  }

  onMounted(() => {
    refresh()
    timer = setInterval(refresh, intervalMs)
    document.addEventListener('visibilitychange', onVisibilityChange)
  })

  onUnmounted(() => {
    clearInterval(timer)
    document.removeEventListener('visibilitychange', onVisibilityChange)
  })

  return { data, error, refresh }
}
