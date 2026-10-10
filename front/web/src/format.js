export function formatUptime(totalSeconds) {
  const s = Math.max(0, Math.floor(totalSeconds))
  const days = Math.floor(s / 86400)
  const hours = Math.floor((s % 86400) / 3600)
  const minutes = Math.floor((s % 3600) / 60)
  if (days > 0) return `${days}d ${hours}h ${minutes}m`
  if (hours > 0) return `${hours}h ${minutes}m`
  if (minutes > 0) return `${minutes}m ${s % 60}s`
  return `${s}s`
}

export function formatKiB(bytes) {
  return `${(bytes / 1024).toFixed(1)} KiB`
}

export function formatBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return formatKiB(bytes)
  return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`
}

// Rough rule of thumb for 2.4 GHz links.
export function signalQuality(rssi) {
  if (rssi >= -55) return { label: 'Excellent', bars: 4 }
  if (rssi >= -67) return { label: 'Good', bars: 3 }
  if (rssi >= -75) return { label: 'Fair', bars: 2 }
  return { label: 'Weak', bars: 1 }
}

export function rgbToHex(r, g, b) {
  return '#' + [r, g, b].map((v) => v.toString(16).padStart(2, '0')).join('')
}

export function hexToRgb(hex) {
  const value = parseInt(hex.slice(1), 16)
  return [(value >> 16) & 255, (value >> 8) & 255, value & 255]
}
