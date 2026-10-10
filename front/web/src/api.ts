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

// ---- Router lab (firmware built with router lab mode) ----

export interface RouterClient {
  mac: string
  /** null until the client has a DHCP lease. */
  ip: string | null
  rssi: number
}

export interface RouterFlow {
  client: string
  dst: string
  /** 0 for ICMP. */
  port: number
  /** IP protocol number: 1 ICMP, 6 TCP, 17 UDP. */
  proto: number
  packets: number
  /** Client to destination only. */
  bytes: number
  /** Seconds since boot, like `now_s`. */
  first_s: number
  last_s: number
}

export interface RouterDns {
  client: string
  name: string
  at_s: number
}

/** A request the lab service received. */
export interface LabMessage {
  client: string
  at_s: number
  length: number
  /** The start of the body. */
  text: string
}

export interface RouterLab {
  enabled: true
  active: boolean
  ssid: string
  ip: string
  channel: number
  max_clients: number
  /** Why the lab network did not open although the device is online. */
  problem: string | null
  capture: boolean
  /** The device's uptime: the `*_s` times count from the same boot. */
  now_s: number
  packets: number
  bytes: number
  flows_dropped: number
  flows_max: number
  clients: RouterClient[]
  flows: RouterFlow[]
  /** Newest first. */
  dns: RouterDns[]
  /** Newest first. */
  lab: LabMessage[]
}

/** Everything the device recorded (every list is bounded), so it is also the export. */
export const getRouter = () => authApi<RouterLab | { enabled: false }>('/api/v1/router')
export const setCapture = (enabled: boolean) => authApi('/api/v1/router/capture', { body: { enabled } })
export const clearCapture = () => authApi('/api/v1/router/clear', { body: {} })
/** The lab's plain-HTTP service (public): the device keeps the start of the body while capture is on. */
export const sendLab = (text: string) => authApi<{ length: number }>('/api/v1/router/lab', { body: { text } })
