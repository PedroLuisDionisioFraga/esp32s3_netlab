import type { MemoryRegion, MemorySnapshot } from './memory'
import type { Sample } from './chart'

// What the memory page remembers between polls, so a tile can open on a chart and not only on the last reading.
// The firmware sends one snapshot per request and keeps no history of its own beyond the 40 samples of `trend`, which
// seed the chart so it is never empty on open. Pure functions over plain arrays: the page keeps the array in a ref
// and replaces it on every sample.

/** How often the page asks the device for a snapshot; the history is spaced by it. */
export const POLL_MS = 2000

/** The KPI tiles that open a detail modal. */
export type MetricKind = 'free' | 'largest' | 'frag' | 'psram' | 'cpu'

/** 900 samples at the page's 2 s poll are 30 minutes. */
export const HISTORY_CAPACITY = 900

/** The readings of one heap region that the charts draw. */
export interface RegionSample {
  total: number
  free: number
  largest: number
  /** heaptop's fragmentation, percent x10. */
  frag10: number
}

export interface HistoryPoint {
  seq: number
  /** Device uptime of the sample, in milliseconds. */
  t: number
  internal: RegionSample
  dma: RegionSample | null
  psram: RegionSample | null
  /** Load per core, percent x10; empty when the firmware has no run-time stats (or the point is a seed). */
  cpu10: number[]
}

/** heaptop's own formula: the share of the free memory that is not in the largest block. */
export function fragmentation10(free: number, largest: number): number {
  if (free <= 0 || largest >= free) return 0
  return Math.floor(((free - largest) * 1000) / free)
}

function sampleOf(region: MemoryRegion | null): RegionSample | null {
  return region ? { total: region.total, free: region.free, largest: region.largest, frag10: region.frag10 } : null
}

/** One point from a snapshot, or null when it has no internal region to chart. */
export function pointFromSnapshot(snapshot: MemorySnapshot): HistoryPoint | null {
  const internal = sampleOf(snapshot.regions.internal)
  if (!internal) return null
  return {
    seq: snapshot.seq,
    t: snapshot.t_ms,
    internal,
    dma: sampleOf(snapshot.regions.dma),
    psram: sampleOf(snapshot.regions.psram),
    cpu10: snapshot.cpu10 ?? [],
  }
}

/** The up-to-40 samples heaptop carries in `trend`, as history points spaced by its sample period. */
export function seedFromTrend(snapshot: MemorySnapshot): HistoryPoint[] {
  const trend = snapshot.trend
  const region = snapshot.regions.internal
  if (!trend || !region) return []
  const n = Math.min(trend.len, trend.internal_free.length, trend.internal_largest.length)
  const points: HistoryPoint[] = []
  // The last entry is the snapshot itself: leave it to the caller, which appends the real point.
  for (let i = 0; i < n - 1; i++) {
    const back = n - 1 - i
    const free = trend.internal_free[i] as number
    const largest = trend.internal_largest[i] as number
    points.push({
      seq: snapshot.seq - back,
      t: snapshot.t_ms - back * snapshot.period_ms,
      internal: { total: region.total, free, largest, frag10: fragmentation10(free, largest) },
      dma: null,
      psram: null,
      cpu10: [],
    })
  }
  return points.filter((point) => point.seq >= 1 && point.t >= 0)
}

/**
 * The history with `snapshot` added. A snapshot already seen (same `seq`) changes nothing. A snapshot older than the
 * last point means the device restarted: the history starts over from its trend. The result is a new array, at most
 * `capacity` long.
 */
export function appendSample(
  points: readonly HistoryPoint[],
  snapshot: MemorySnapshot,
  capacity = HISTORY_CAPACITY,
): readonly HistoryPoint[] {
  const point = pointFromSnapshot(snapshot)
  if (!point) return points
  const last = points[points.length - 1]
  if (last && point.t >= last.t && point.seq <= last.seq) return points
  const base = last && point.t < last.t ? [] : points
  const seeded = base.length === 0 ? seedFromTrend(snapshot) : base
  return [...seeded, point].slice(-capacity)
}

export interface HistoryRange {
  key: string
  label: string
  /** Window length in milliseconds; null = everything remembered. */
  ms: number | null
}

export const HISTORY_RANGES: readonly HistoryRange[] = [
  { key: '1m', label: '1 min', ms: 60_000 },
  { key: '5m', label: '5 min', ms: 300_000 },
  { key: '15m', label: '15 min', ms: 900_000 },
  { key: 'all', label: 'All', ms: null },
]

/** The points of the last `ms` milliseconds before the newest one. */
export function sliceRange(points: readonly HistoryPoint[], ms: number | null): readonly HistoryPoint[] {
  const last = points[points.length - 1]
  if (ms === null || !last) return points
  const from = last.t - ms
  const index = points.findIndex((point) => point.t >= from)
  return index <= 0 ? points : points.slice(index)
}

export interface SeriesStats {
  count: number
  first: number
  last: number
  min: number
  max: number
  mean: number
  /** last - first. */
  delta: number
  /** Least-squares slope, in units per minute; null with fewer than two points or no spread in time. */
  slopePerMin: number | null
  /** How much of the variation the line explains, 0-1; null when the series is flat. */
  r2: number | null
}

/** Min, max, mean and trend of the readings that exist (gaps are skipped); null when there are none. */
export function seriesStats(ts: readonly number[], values: readonly Sample[]): SeriesStats | null {
  const xs: number[] = []
  const ys: number[] = []
  for (let i = 0; i < ts.length; i++) {
    const value = values[i]
    if (value !== null && value !== undefined && Number.isFinite(value)) {
      xs.push((ts[i] as number) / 60_000)
      ys.push(value)
    }
  }
  const n = ys.length
  if (n === 0) return null

  const sum = ys.reduce((total, value) => total + value, 0)
  const mean = sum / n
  const first = ys[0] as number
  const last = ys[n - 1] as number

  let slopePerMin: number | null = null
  let r2: number | null = null
  if (n >= 2) {
    const meanX = xs.reduce((total, value) => total + value, 0) / n
    let sxx = 0
    let sxy = 0
    let syy = 0
    for (let i = 0; i < n; i++) {
      const dx = (xs[i] as number) - meanX
      const dy = (ys[i] as number) - mean
      sxx += dx * dx
      sxy += dx * dy
      syy += dy * dy
    }
    if (sxx > 0) {
      slopePerMin = sxy / sxx
      r2 = syy > 0 ? (sxy * sxy) / (sxx * syy) : null
    }
  }

  return { count: n, first, last, min: Math.min(...ys), max: Math.max(...ys), mean, delta: last - first, slopePerMin, r2 }
}

/** A projection needs a real trend: enough points, and a line that explains most of the movement. */
export const MIN_PROJECTION_POINTS = 30
export const MIN_PROJECTION_R2 = 0.5

/**
 * Minutes until the series reaches `limit` if it keeps its current slope, or null when there is no honest answer: too
 * few points, a noisy line, a slope that moves away from the limit, or a value already past it.
 * `floor` is a limit the value must stay above (free memory), `ceiling` one it must stay below.
 */
export function minutesToLimit(stats: SeriesStats | null, limit: number, kind: 'floor' | 'ceiling'): number | null {
  if (!stats || stats.slopePerMin === null || stats.r2 === null) return null
  if (stats.count < MIN_PROJECTION_POINTS || stats.r2 < MIN_PROJECTION_R2) return null
  if (kind === 'floor') {
    if (stats.slopePerMin >= 0 || stats.last <= limit) return null
    return (stats.last - limit) / -stats.slopePerMin
  }
  if (stats.slopePerMin <= 0 || stats.last >= limit) return null
  return (limit - stats.last) / stats.slopePerMin
}

/** "about 12 min", "about 3 h", "more than 1 day": a projection as a sentence fragment. */
export function formatMinutes(minutes: number): string {
  if (minutes < 1) return 'less than 1 min'
  if (minutes < 90) return `about ${Math.round(minutes)} min`
  const hours = minutes / 60
  if (hours < 24) return `about ${Math.round(hours)} h`
  return 'more than 1 day'
}

/** The window as CSV (dot decimals, one row per sample): uptime in seconds, bytes, percent. */
export function historyToCsv(points: readonly HistoryPoint[]): string {
  const cores = points.reduce((most, point) => Math.max(most, point.cpu10.length), 0)
  const header = [
    'seq',
    'uptime_s',
    'internal_free_b',
    'internal_largest_b',
    'internal_frag_pct',
    ...Array.from({ length: cores }, (_, core) => `cpu_core${core}_pct`),
  ]
  const rows = points.map((point) =>
    [
      point.seq,
      point.t / 1000,
      point.internal.free,
      point.internal.largest,
      point.internal.frag10 / 10,
      ...Array.from({ length: cores }, (_, core) => (point.cpu10[core] === undefined ? '' : (point.cpu10[core] as number) / 10)),
    ].join(','),
  )
  return [header.join(','), ...rows].join('\n') + '\n'
}
