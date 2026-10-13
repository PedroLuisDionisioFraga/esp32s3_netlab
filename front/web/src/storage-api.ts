import { authApi } from './api'
import type { StorageSnapshot } from './storage'

// The storage route of the device (components/rest_server, heaptop 0.5.0). It needs a session.

export const STORAGE_ENDPOINT = '/api/v1/storage'

/** How often the page asks: flash and NVS change slowly, and the NVS part walks the partition. */
export const STORAGE_POLL_MS = 30_000

export function fetchStorage(): Promise<StorageSnapshot> {
  return authApi<StorageSnapshot>(STORAGE_ENDPOINT)
}
