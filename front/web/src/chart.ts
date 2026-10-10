// Maths of the history chart: scales, round ticks, line/area paths and the sample nearest to the pointer. Pure (no Vue,
// no DOM), so it is the part of the chart that can be tested without a browser.

/** A series value that is missing (a gap in the data): the line breaks there instead of joining its neighbours. */
export type Sample = number | null

/** One line of a chart. */
export interface ChartSeries {
  id: string
  label: string
  /** Any CSS colour, normally a `var(--sbc-series-n)`. */
  color: string
  values: readonly Sample[]
  /** Draw the wash under the line (one-series charts only: two washes over each other hide the lines). */
  area?: boolean
  /** How a value reads in the tooltip and at the end of the line. */
  format: (value: number) => string
}

/** A threshold drawn across the chart, such as heaptop's free-memory floor. */
export interface ChartLimit {
  value: number
  label: string
}

/** Maps [d0, d1] onto [r0, r1] linearly. A flat domain maps everything to the middle of the range. */
export function scaleLinear(d0: number, d1: number, r0: number, r1: number): (value: number) => number {
  if (d1 === d0) return () => (r0 + r1) / 2
  const k = (r1 - r0) / (d1 - d0)
  return (value) => r0 + (value - d0) * k
}

export interface NiceTicks {
  ticks: number[]
  /** First and last tick: the axis range that covers the data. */
  min: number
  max: number
}

/** Removes the float noise of a computed tick (0.30000000000000004 -> 0.3). */
function clean(value: number): number {
  return Number(value.toPrecision(12))
}

/**
 * Round tick values covering [min, max], about `count` of them. `unit` is the size of the unit the axis is labelled
 * in (1024 for KiB): steps are 1, 2, 2.5, 5 or 10 times a power of ten of that unit, so a KiB axis reads
 * 150 / 200 / 250 and not 153.6 / 204.8 / 256. The ticks come back in the original unit.
 */
export function niceTicks(min: number, max: number, count = 4, unit = 1): NiceTicks {
  let lo = min / unit
  let hi = max / unit
  if (!Number.isFinite(lo) || !Number.isFinite(hi)) {
    lo = 0
    hi = 1
  }
  if (lo > hi) [lo, hi] = [hi, lo]
  if (hi === lo) {
    // A flat series: open a window around it so the axis has a range to draw.
    const pad = lo === 0 ? 1 : Math.abs(lo) * 0.05
    lo -= pad
    hi += pad
  }

  const raw = (hi - lo) / Math.max(1, count)
  const magnitude = 10 ** Math.floor(Math.log10(raw))
  const residual = raw / magnitude
  const nice = residual <= 1 ? 1 : residual <= 2 ? 2 : residual <= 2.5 ? 2.5 : residual <= 5 ? 5 : 10
  const step = nice * magnitude

  const first = Math.floor(lo / step + 1e-9)
  const last = Math.ceil(hi / step - 1e-9)
  const ticks: number[] = []
  for (let i = first; i <= last; i++) ticks.push(clean(i * step * unit))
  return { ticks, min: ticks[0] as number, max: ticks[ticks.length - 1] as number }
}

const TIME_STEPS_MS = [5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600].map((seconds) => seconds * 1000)

/** Tick positions of a time axis, as milliseconds back from its right edge (0 = now): 0, step, 2 step, ... */
export function timeTicks(spanMs: number, count = 5): number[] {
  const span = Math.max(0, spanMs)
  const step = TIME_STEPS_MS.find((candidate) => span / candidate <= count) ?? (TIME_STEPS_MS[TIME_STEPS_MS.length - 1] as number)
  const ticks: number[] = []
  for (let offset = 0; offset <= span + 1e-6; offset += step) ticks.push(offset)
  return ticks
}

/** "now", "-30 s", "-2 min", "-1 min 30 s": how long before the right edge a tick lies. */
export function formatAgo(offsetMs: number): string {
  const seconds = Math.round(offsetMs / 1000)
  if (seconds <= 0) return 'now'
  if (seconds < 60) return `-${seconds} s`
  const minutes = Math.floor(seconds / 60)
  const rest = seconds % 60
  return rest === 0 ? `-${minutes} min` : `-${minutes} min ${rest} s`
}

/** Index of the entry of the ascending `xs` closest to `x`, or -1 for an empty list. */
export function nearestIndex(xs: readonly number[], x: number): number {
  if (xs.length === 0) return -1
  let lo = 0
  let hi = xs.length - 1
  while (lo < hi) {
    const mid = (lo + hi) >> 1
    if ((xs[mid] as number) < x) lo = mid + 1
    else hi = mid
  }
  // lo is the first entry >= x; the one before it may be closer.
  if (lo > 0 && Math.abs((xs[lo - 1] as number) - x) <= Math.abs((xs[lo] as number) - x)) return lo - 1
  return lo
}

/**
 * Runs of consecutive indexes that belong on one line: a run ends where a value is missing or where two samples
 * are further apart than `maxGapMs` (the page was hidden and stopped polling), so the chart never draws a line over
 * time it has no data for. Each run is [first, last], both inclusive.
 */
export function splitSegments(ts: readonly number[], values: readonly Sample[], maxGapMs: number): Array<[number, number]> {
  const runs: Array<[number, number]> = []
  let start = -1
  for (let i = 0; i < ts.length; i++) {
    const missing = values[i] === null || values[i] === undefined || !Number.isFinite(values[i] as number)
    if (missing) {
      if (start >= 0) runs.push([start, i - 1])
      start = -1
      continue
    }
    if (start >= 0 && (ts[i] as number) - (ts[i - 1] as number) > maxGapMs) {
      runs.push([start, i - 1])
      start = i
    } else if (start < 0) {
      start = i
    }
  }
  if (start >= 0) runs.push([start, ts.length - 1])
  return runs
}

const round1 = (value: number): number => Math.round(value * 10) / 10

/** `M x y L x y ...` through the points first..last of the two coordinate lists. A single point becomes a dot. */
export function linePath(xs: readonly number[], ys: readonly number[], first: number, last: number): string {
  if (first > last) return ''
  if (first === last) return `M${round1(xs[first] as number)} ${round1(ys[first] as number)}l0.01 0`
  let d = ''
  for (let i = first; i <= last; i++) d += `${i === first ? 'M' : 'L'}${round1(xs[i] as number)} ${round1(ys[i] as number)}`
  return d
}

/** The same run closed down to `baseY`, for the area wash under a line. */
export function areaPath(xs: readonly number[], ys: readonly number[], first: number, last: number, baseY: number): string {
  if (first >= last) return ''
  return `${linePath(xs, ys, first, last)}L${round1(xs[last] as number)} ${round1(baseY)}L${round1(xs[first] as number)} ${round1(baseY)}Z`
}

export interface DomainOptions {
  /** Fixed range (a percentage is always 0-100): the data does not move it. */
  fixed?: readonly [number, number]
  /** Values that must stay inside the range without being part of the data, such as a limit line. */
  include?: readonly number[]
  /** Share of the range added above and below the data. */
  pad?: number
}

export interface AxisOptions extends DomainOptions {
  /** Unit the axis is labelled in (1024 for KiB); see niceTicks(). */
  unit?: number
  /** About how many ticks. */
  count?: number
}

/** The y axis of a chart: round ticks over the domain of `values` (plus what `include` keeps in view). */
export function axisFor(values: readonly Sample[], options: AxisOptions = {}): NiceTicks {
  const [lo, hi] = domainFor(values, options)
  return niceTicks(lo, hi, options.count ?? 4, options.unit ?? 1)
}

/** The y range of a chart: the data and what must stay in view, with a little air around them. */
export function domainFor(values: readonly Sample[], options: DomainOptions = {}): [number, number] {
  if (options.fixed) return [options.fixed[0], options.fixed[1]]
  const all = [...values, ...(options.include ?? [])].filter((value): value is number => value !== null && Number.isFinite(value))
  if (all.length === 0) return [0, 1]
  let lo = Math.min(...all)
  let hi = Math.max(...all)
  const pad = (hi - lo) * (options.pad ?? 0.08)
  lo -= pad
  hi += pad
  return [lo, hi]
}
