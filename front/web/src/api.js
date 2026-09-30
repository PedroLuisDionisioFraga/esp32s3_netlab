// Thin wrapper over the device's JSON API. Every call has a timeout so a wedged
// device shows up as "unreachable" instead of a spinner that never ends.
const TIMEOUT_MS = 4000

async function request(path, options = {}) {
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS)
  try {
    const res = await fetch(`/api/v1${path}`, { ...options, signal: controller.signal })
    if (!res.ok) {
      throw new Error(`${res.status} ${(await res.text()).trim()}`)
    }
    return await res.json()
  } catch (err) {
    if (err.name === 'AbortError') {
      throw new Error('Device did not answer in time')
    }
    throw err
  } finally {
    clearTimeout(timer)
  }
}

function postJson(path, body) {
  return request(path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
}

export const getSystemInfo = () => request('/system/info')
export const getLink = () => request('/link')
export const setLedColor = (r, g, b) => postJson('/led', { r, g, b })
export const setLedAuto = () => postJson('/led', { mode: 'auto' })
// Erases the saved router and restarts the device into Wi-Fi setup mode.
export const forgetWifi = () => postJson('/wifi/forget', {})
