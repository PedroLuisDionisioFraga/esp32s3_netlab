import { ref } from 'vue'
import { request } from './http'

// The login session. The device hands out a token at login; every API call sends it back as
// "Authorization: Bearer <token>" (a header the page sets itself, not a cookie, so another site cannot make the
// browser send it). It lives in sessionStorage: it ends with the tab. The device keeps it alive while it is used.

const KEY = 'netlab:session'

// Storage can be missing or blocked (a private window): then the token only lives in memory, until the page reloads.
let memoryToken: string | null = null

function readToken(): string | null {
  try {
    return sessionStorage.getItem(KEY) ?? memoryToken
  } catch {
    return memoryToken
  }
}

/** Reactive, so the sidebar can show "Sign out" only while there is something to sign out of. */
export const sessionActive = ref(readToken() !== null)

export function getToken(): string | null {
  return readToken()
}

export function authHeaders(): Record<string, string> {
  const token = readToken()
  return token === null ? {} : { Authorization: `Bearer ${token}` }
}

export function hasSession(): boolean {
  return readToken() !== null
}

function store(token: string | null): void {
  memoryToken = token
  try {
    if (token === null) sessionStorage.removeItem(KEY)
    else sessionStorage.setItem(KEY, token)
  } catch {
    /* in memory only */
  }
  sessionActive.value = token !== null
}

export function clearSession(): void {
  store(null)
}

/** Throws an ApiError with the device's message ("Wrong user name or password", "Too many attempts, ...") on failure. */
export async function login(username: string, password: string): Promise<void> {
  const reply = await request<{ token: string }>('/api/v1/session', { body: { username, password } })
  store(reply.token)
}

/** Ends the session on the device (best effort: a device that cannot be reached forgets it on its own) and here. */
export async function logout(): Promise<void> {
  try {
    await request('/api/v1/session', { method: 'DELETE', headers: authHeaders() })
  } catch {
    /* signing out must work even when the device does not answer */
  }
  clearSession()
}

export interface About {
  name: string
  version: string
  hostname: string
  /** False when the firmware was built without a login: no session is needed at all. */
  auth: boolean
}

/** Public: the login page reads it before anyone is signed in. */
export function fetchAbout(): Promise<About> {
  return request<About>('/api/v1/about')
}

let authRequired: boolean | null = null

/** Whether the device wants a login. Asked once; an unreachable device is treated as "yes", and the login page says why. */
export async function isAuthRequired(): Promise<boolean> {
  if (authRequired !== null) return authRequired
  try {
    authRequired = (await fetchAbout()).auth
    return authRequired
  } catch {
    return true
  }
}
