import { onMounted, onUnmounted, ref, toValue, watch } from 'vue'

// Calls `fetcher` every `intervalMs` (a number, ref or getter: a change restarts the timer). It never
// overlaps two requests and pauses while the tab is hidden or `enabled` is false, so an open tab does
// not keep hammering a small device.
export function usePolling(fetcher, intervalMs, { enabled = true } = {}) {
  const data = ref(null)
  const error = ref(null)
  let timer = null
  let inFlight = false
  let again = false
  let unmounted = false

  async function load() {
    if (inFlight || document.hidden || unmounted) {
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
    if (again) {
      again = false
      load()
    }
  }

  // Fetch now, even while disabled. If a request is already running, fetch once more right after
  // it, so the answer reflects what the caller just changed.
  function refresh() {
    if (inFlight) {
      again = true
      return
    }
    load()
  }

  function restart() {
    clearInterval(timer)
    timer = toValue(enabled) ? setInterval(load, toValue(intervalMs)) : null
  }

  function onVisibilityChange() {
    if (!document.hidden && toValue(enabled)) {
      load()
    }
  }

  watch(() => [toValue(intervalMs), toValue(enabled)], restart)

  onMounted(() => {
    if (toValue(enabled)) {
      load()
    }
    restart()
    document.addEventListener('visibilitychange', onVisibilityChange)
  })

  onUnmounted(() => {
    unmounted = true
    clearInterval(timer)
    timer = null
    document.removeEventListener('visibilitychange', onVisibilityChange)
  })

  return { data, error, refresh }
}
