import { getCurrentScope, onScopeDispose, ref, type Ref } from 'vue'

export interface Polling {
  running: Readonly<Ref<boolean>>
  error: Readonly<Ref<unknown>>
  lastUpdated: Readonly<Ref<number | null>>
  start: () => void
  stop: () => void
}

/**
 * Calls `fn` now and then every `intervalMs` after it settles. The next call is scheduled only when the previous one
 * is done, so a slow device never gets two requests at once (it serves one at a time). Nothing runs while the tab is
 * hidden; becoming visible again fetches at once. Stops with the component that created it.
 */
export function usePolling(fn: () => unknown, intervalMs: number): Polling {
  const running = ref(false)
  const error = ref<unknown>(null)
  const lastUpdated = ref<number | null>(null)

  let timer: ReturnType<typeof setTimeout> | undefined
  let inFlight = false
  // A call was asked for (restart, tab visible again) while another was still running.
  let queued = false
  // Bumped by stop(): a call still in flight then ends without touching `error` or `lastUpdated`.
  let generation = 0

  const hidden = (): boolean => document.visibilityState === 'hidden'

  function schedule(delay: number): void {
    clearTimeout(timer)
    timer = setTimeout(() => void tick(), delay)
  }

  async function tick(): Promise<void> {
    timer = undefined
    if (!running.value || hidden()) return
    if (inFlight) {
      queued = true
      return
    }
    inFlight = true
    const mine = generation
    try {
      await fn()
      if (mine === generation) {
        error.value = null
        lastUpdated.value = Date.now()
      }
    } catch (caught) {
      if (mine === generation) error.value = caught
    } finally {
      inFlight = false
    }
    if (!running.value || hidden()) return
    if (queued) {
      queued = false
      schedule(0)
    } else {
      schedule(intervalMs)
    }
  }

  function onVisibility(): void {
    if (!running.value) return
    if (hidden()) {
      clearTimeout(timer)
      timer = undefined
    } else if (inFlight) {
      queued = true
    } else {
      schedule(0)
    }
  }

  function start(): void {
    if (running.value) return
    running.value = true
    document.addEventListener('visibilitychange', onVisibility)
    if (inFlight) queued = true
    else void tick()
  }

  function stop(): void {
    if (!running.value) return
    running.value = false
    queued = false
    generation += 1
    clearTimeout(timer)
    timer = undefined
    document.removeEventListener('visibilitychange', onVisibility)
  }

  start()
  if (getCurrentScope()) onScopeDispose(stop)

  return { running, error, lastUpdated, start, stop }
}
