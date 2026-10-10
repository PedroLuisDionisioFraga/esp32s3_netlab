import { reactive, readonly } from 'vue'

// Whether the device is answering. Only the HTTP layer writes it: any HTTP answer, even a 4xx or 5xx, proves the
// device is reachable; a timeout or a network error says it is not.

export type ConnectionStatus = 'unknown' | 'online' | 'offline'

const state = reactive<{ status: ConnectionStatus; lastOkAt: number | null }>({ status: 'unknown', lastOkAt: null })

export const connection = readonly(state)

export function markOnline(): void {
  state.status = 'online'
  state.lastOkAt = Date.now()
}

export function markOffline(): void {
  state.status = 'offline'
}
