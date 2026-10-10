import { markOffline, markOnline } from './connection'

// Thin wrapper over fetch for the device's JSON API. Every call has a timeout, so a wedged device shows up as
// "unreachable" and not as a spinner that never ends. It knows nothing about sessions: api.ts adds those.

export type ApiErrorKind = 'http' | 'timeout' | 'network' | 'parse'

export class ApiError extends Error {
  readonly kind: ApiErrorKind
  readonly status?: number

  constructor(kind: ApiErrorKind, message: string, status?: number) {
    super(message)
    this.name = 'ApiError'
    this.kind = kind
    this.status = status
  }
}

export interface RequestOptions {
  /** Default: POST when there is a body, GET otherwise. */
  method?: 'GET' | 'POST' | 'PUT' | 'DELETE'
  /** Sent as JSON. */
  body?: unknown
  headers?: Record<string, string>
  /** Default 4000 ms. A Wi-Fi scan blocks the device for a few seconds and asks for more. */
  timeoutMs?: number
}

const DEFAULT_TIMEOUT_MS = 4000

/** The device answers errors in plain text ("Sign in required"); that text is the message. */
function errorMessage(status: number, text: string): string {
  const trimmed = text.trim()
  return trimmed !== '' && trimmed.length < 200 ? trimmed : `The device answered ${status}`
}

export async function request<T = unknown>(path: string, options: RequestOptions = {}): Promise<T> {
  const method = options.method ?? (options.body === undefined ? 'GET' : 'POST')
  const headers: Record<string, string> = {
    ...(options.body === undefined ? {} : { 'Content-Type': 'application/json' }),
    ...options.headers,
  }

  const controller = new AbortController()
  let timedOut = false
  const timer = setTimeout(() => {
    timedOut = true
    controller.abort()
  }, options.timeoutMs ?? DEFAULT_TIMEOUT_MS)

  const unreachable = (): ApiError => {
    markOffline()
    return timedOut
      ? new ApiError('timeout', 'The device did not answer in time')
      : new ApiError('network', 'Could not reach the device')
  }

  try {
    let response: Response
    try {
      response = await fetch(path, {
        method,
        headers,
        body: options.body === undefined ? undefined : JSON.stringify(options.body),
        signal: controller.signal,
        credentials: 'omit',
        cache: 'no-store',
      })
    } catch {
      throw unreachable()
    }
    // Any HTTP answer, even an error, means the device is there.
    markOnline()

    let text: string
    try {
      text = await response.text()
    } catch {
      throw unreachable()
    }

    if (!response.ok) throw new ApiError('http', errorMessage(response.status, text), response.status)

    if (text === '') return undefined as T
    const type = response.headers.get('content-type') ?? ''
    if (!type.includes('application/json')) return text as T
    try {
      return JSON.parse(text) as T
    } catch {
      throw new ApiError('parse', 'The device sent something that is not valid JSON', response.status)
    }
  } finally {
    clearTimeout(timer)
  }
}
