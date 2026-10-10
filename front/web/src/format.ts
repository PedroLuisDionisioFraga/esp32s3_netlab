// Number and text formatting shared by the pages. Pure: no Vue, no DOM.

const NUMBER_LOCALE = 'en-US'

/** "3d 4h 12m", "2h 5m", "7m 30s", "42s". */
export function formatUptime(totalSeconds: number): string {
  const s = Math.max(0, Math.floor(totalSeconds))
  const days = Math.floor(s / 86400)
  const hours = Math.floor((s % 86400) / 3600)
  const minutes = Math.floor((s % 3600) / 60)
  if (days > 0) return `${days}d ${hours}h ${minutes}m`
  if (hours > 0) return `${hours}h ${minutes}m`
  if (minutes > 0) return `${minutes}m ${s % 60}s`
  return `${s}s`
}

/** A size in bytes as B, KiB or MiB, to one decimal. */
export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  const kib = bytes / 1024
  if (kib < 1024) return `${kib.toLocaleString(NUMBER_LOCALE, { maximumFractionDigits: 1 })} KiB`
  return `${(kib / 1024).toLocaleString(NUMBER_LOCALE, { maximumFractionDigits: 1 })} MiB`
}

/** heaptop's percentages are integers x10 (523 = 52.3 %). */
export function formatPercent10(value: number): string {
  return `${(value / 10).toLocaleString(NUMBER_LOCALE, { minimumFractionDigits: 1, maximumFractionDigits: 1 })} %`
}

/** A plain number with at most `digits` decimals. */
export function formatNumber(value: number, digits = 1): string {
  return value.toLocaleString(NUMBER_LOCALE, { maximumFractionDigits: digits })
}

export interface SignalQuality {
  label: string
  /** 1 to 4. */
  bars: number
}

/** Rough rule of thumb for 2.4 GHz links. */
export function signalQuality(rssi: number): SignalQuality {
  if (rssi >= -55) return { label: 'Excellent', bars: 4 }
  if (rssi >= -67) return { label: 'Good', bars: 3 }
  if (rssi >= -75) return { label: 'Fair', bars: 2 }
  return { label: 'Weak', bars: 1 }
}

export function rgbToHex(r: number, g: number, b: number): string {
  return '#' + [r, g, b].map((v) => v.toString(16).padStart(2, '0')).join('')
}

export function hexToRgb(hex: string): [number, number, number] {
  const value = parseInt(hex.slice(1), 16)
  return [(value >> 16) & 255, (value >> 8) & 255, value & 255]
}
