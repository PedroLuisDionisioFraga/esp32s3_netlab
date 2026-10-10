import { reactive, readonly } from 'vue'

export type ToastTone = 'info' | 'success' | 'error'

export interface Toast {
  id: number
  message: string
  tone: ToastTone
}

const DURATION_MS = 4000

const state = reactive<{ current: Toast | null }>({ current: null })
let nextId = 1
let timer: ReturnType<typeof setTimeout> | undefined

/** Read by NlToastHost, which renders the one toast there is. */
export const toastState = readonly(state)

export function dismissToast(): void {
  clearTimeout(timer)
  timer = undefined
  state.current = null
}

// An error stays until it is closed (a message that vanishes before it is read is lost); the others fade after 4 s.
function startTimer(): void {
  timer = state.current && state.current.tone !== 'error' ? setTimeout(dismissToast, DURATION_MS) : undefined
}

/** The toast holds while the pointer or the focus is on it. */
export function pauseToast(): void {
  clearTimeout(timer)
  timer = undefined
}

export function resumeToast(): void {
  clearTimeout(timer)
  startTimer()
}

/** One slot: a new toast replaces the current one. */
export function useToast() {
  function show(message: string, options: { tone?: ToastTone } = {}): void {
    clearTimeout(timer)
    state.current = { id: nextId++, message, tone: options.tone ?? 'info' }
    startTimer()
  }
  return { show }
}
