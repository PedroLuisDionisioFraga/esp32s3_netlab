import { ApiError, request, type RequestOptions } from './http'
import { authHeaders, clearSession } from './session'

// The API as the pages use it: public calls go through request() (http.ts); these add the session token.

export { ApiError }

type Listener = () => void
const expiredListeners = new Set<Listener>()

/** Called whenever the device answers 401, so the app can go back to the login. */
export function onSessionExpired(listener: Listener): void {
  expiredListeners.add(listener)
}

export async function authApi<T = unknown>(path: string, options: RequestOptions = {}): Promise<T> {
  try {
    return await request<T>(path, { ...options, headers: { ...authHeaders(), ...options.headers } })
  } catch (error) {
    if (error instanceof ApiError && error.status === 401) {
      clearSession()
      expiredListeners.forEach((listener) => listener())
    }
    throw error
  }
}

export function describeError(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}

// ---- Calls ----

export interface LedState {
  mode: 'manual' | 'auto'
  r: number
  g: number
  b: number
}

export interface SystemInfo {
  chip: string
  idf_version: string
  cores: number
  /** Die temperature in degrees Celsius, or null when the sensor is off. */
  temperature_c: number | null
  uptime_s: number
  free_heap: number
  min_free_heap: number
  reset_reason: string
  led: LedState
}

export interface LinkInfo {
  associated: boolean
  online: boolean
  ssid?: string
  bssid?: string
  channel?: number
  rssi?: number
  ip?: string
  gateway?: string
  netmask?: string
}

export interface ScannedNetwork {
  ssid: string
  rssi: number
  channel: number
  secure: boolean
}

export const getSystemInfo = () => authApi<SystemInfo>('/api/v1/system/info')
export const getLink = () => authApi<LinkInfo>('/api/v1/link')
export const setLedColor = (r: number, g: number, b: number) => authApi('/api/v1/led', { body: { r, g, b } })
export const setLedAuto = () => authApi('/api/v1/led', { body: { mode: 'auto' } })
/** A Wi-Fi scan blocks the device for a few seconds. */
export const scanNetworks = () =>
  authApi<{ networks: ScannedNetwork[] }>('/api/v1/wifi/scan', { timeoutMs: 20_000 })
/** Erases the saved router and restarts the device into Wi-Fi setup mode. */
export const forgetWifi = () => authApi('/api/v1/wifi/forget', { body: {} })
export const sendChat = (message: string) => authApi<{ message: string }>('/api/v1/chat', { body: { message } })
