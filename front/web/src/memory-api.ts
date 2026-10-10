import { authApi } from './api'
import type { MemoryClearResult, MemorySnapshot } from './memory'

// The two memory routes of the device (components/rest_server). Both need a session.

export const MEMORY_ENDPOINT = '/api/v1/memory'
export const MEMORY_CLEAR_ENDPOINT = '/api/v1/memory/clear'

export function fetchMemory(): Promise<MemorySnapshot> {
  return authApi<MemorySnapshot>(MEMORY_ENDPOINT)
}

/** Starts a fresh measurement window: minimum free, failures, trends and leak history begin again. */
export function clearMemory(): Promise<MemoryClearResult> {
  return authApi<MemoryClearResult>(MEMORY_CLEAR_ENDPOINT, { method: 'POST', body: {} })
}
