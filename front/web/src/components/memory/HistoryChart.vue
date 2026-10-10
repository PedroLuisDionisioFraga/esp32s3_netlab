<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import {
  areaPath,
  axisFor,
  formatAgo,
  linePath,
  nearestIndex,
  scaleLinear,
  splitSegments,
  timeTicks,
  type ChartLimit,
  type ChartSeries,
} from '../../chart'

const props = withDefaults(
  defineProps<{
    /** Sample times (uptime, ms), ascending: one per value of every series. */
    ts: readonly number[]
    series: readonly ChartSeries[]
    /** Accessible name; `summary` says what the chart shows in words. */
    title: string
    summary: string
    /** Size of the unit the y axis is labelled in (1024 for KiB), so its ticks are round numbers in that unit. */
    unit?: number
    /** Label of an axis tick. */
    axisFormat: (value: number) => string
    /** Fixed y range, for values with natural bounds (a percentage is always 0-100). */
    domain?: readonly [number, number]
    limit?: ChartLimit | null
    /** Stretch the y range to include the limit even when the data is far from it. */
    includeLimit?: boolean
    /** Gap between samples, ms: a longer silence breaks the line instead of being drawn over. */
    periodMs?: number
    /** Window shown, ms back from the newest sample; null = all of `ts`. */
    spanMs?: number | null
    /** Wall-clock time (ms since the epoch) of the newest sample, to print a clock time in the tooltip. */
    endWallMs?: number | null
    height?: number
  }>(),
  { unit: 1, includeLimit: false, periodMs: 2000, spanMs: null, endWallMs: null, height: 260, domain: undefined, limit: null },
)

const MARGIN = { left: 68, right: 16, top: 14, bottom: 28 }
const GAP_FACTOR = 3 // a silence of more than this many periods is a gap

const wrap = ref<HTMLElement | null>(null)
const width = ref(640)
let observer: ResizeObserver | undefined

function measure(): void {
  const measured = wrap.value?.clientWidth
  if (measured && measured > 0) width.value = measured
}

onMounted(() => {
  measure()
  if (typeof ResizeObserver === 'function' && wrap.value) {
    observer = new ResizeObserver(measure)
    observer.observe(wrap.value)
  }
})
onBeforeUnmount(() => observer?.disconnect())

const plot = computed(() => ({
  x0: MARGIN.left,
  x1: Math.max(MARGIN.left + 40, width.value - MARGIN.right),
  y0: MARGIN.top,
  y1: props.height - MARGIN.bottom,
}))

// The window: the newest sample is the right edge; a span longer than the data shows only the data.
const tMax = computed(() => props.ts[props.ts.length - 1] ?? 0)
const tMin = computed(() => {
  const first = props.ts[0] ?? 0
  return props.spanMs === null ? first : Math.max(first, tMax.value - props.spanMs)
})
const xScale = computed(() => scaleLinear(tMin.value, tMax.value, plot.value.x0, plot.value.x1))
const xs = computed(() => props.ts.map((t) => xScale.value(t)))

const ticksY = computed(() =>
  axisFor(
    props.series.flatMap((s) => s.values),
    { fixed: props.domain, include: props.limit && props.includeLimit ? [props.limit.value] : [], unit: props.unit },
  ),
)
const yScale = computed(() => scaleLinear(ticksY.value.min, ticksY.value.max, plot.value.y1, plot.value.y0))

const lines = computed(() =>
  props.series.map((series) => {
    const ys = series.values.map((value) => (value === null ? Number.NaN : yScale.value(value)))
    const runs = splitSegments(props.ts, series.values, props.periodMs * GAP_FACTOR)
    let lastIndex = -1
    for (let i = series.values.length - 1; i >= 0; i--) {
      if (series.values[i] !== null) {
        lastIndex = i
        break
      }
    }
    return {
      series,
      ys,
      lastIndex,
      paths: runs.map(([a, b]) => linePath(xs.value, ys, a, b)),
      areas: series.area ? runs.map(([a, b]) => areaPath(xs.value, ys, a, b, plot.value.y1)) : [],
    }
  }),
)

const yGrid = computed(() => ticksY.value.ticks.map((value) => ({ value, y: yScale.value(value) })))

const xGrid = computed(() => {
  const span = tMax.value - tMin.value
  return timeTicks(span, Math.max(2, Math.floor((plot.value.x1 - plot.value.x0) / 110))).map((offset) => ({
    offset,
    x: xScale.value(tMax.value - offset),
    label: formatAgo(offset),
  }))
})

const limitY = computed(() => {
  const limit = props.limit
  if (!limit) return null
  return limit.value >= ticksY.value.min && limit.value <= ticksY.value.max ? yScale.value(limit.value) : null
})

// One series gets its extremes and its end value written on the chart; with two or more the legend and the tooltip
// carry the numbers (labels on converging lines only collide).
const single = computed(() => (props.series.length === 1 ? (lines.value[0] ?? null) : null))
const extremes = computed(() => {
  const line = single.value
  if (!line || line.lastIndex < 0) return []
  let lo = -1
  let hi = -1
  line.series.values.forEach((value, i) => {
    if (value === null) return
    if (lo < 0 || value < (line.series.values[lo] as number)) lo = i
    if (hi < 0 || value > (line.series.values[hi] as number)) hi = i
  })
  if (lo < 0 || lo === hi) return []
  const endX = xs.value[line.lastIndex] as number
  return [
    { kind: 'min', index: lo },
    { kind: 'max', index: hi },
  ]
    .filter(({ index }) => Math.abs((xs.value[index] as number) - endX) > 90)
    .map(({ kind, index }) => ({
      kind,
      x: xs.value[index] as number,
      y: line.ys[index] as number,
      text: line.series.format(line.series.values[index] as number),
    }))
})

// Hover and keyboard share one index.
const hover = ref<number | null>(null)
const announce = ref('')

function indexAt(clientX: number): number {
  const rect = wrap.value?.getBoundingClientRect()
  if (!rect) return -1
  const { x0, x1 } = plot.value
  const t = tMin.value + ((clientX - rect.left - x0) / (x1 - x0)) * (tMax.value - tMin.value)
  return nearestIndex(props.ts, t)
}

function onPointer(event: PointerEvent): void {
  const index = indexAt(event.clientX)
  hover.value = index < 0 ? null : index
}

function onLeave(): void {
  hover.value = null
}

function describe(index: number): string {
  const when = formatAgo(tMax.value - (props.ts[index] as number))
  const parts = props.series.map((s) => {
    const value = s.values[index]
    return `${s.label} ${value === null || value === undefined ? 'no reading' : s.format(value)}`
  })
  return `${when}: ${parts.join(', ')}`
}

function onKey(event: KeyboardEvent): void {
  const last = props.ts.length - 1
  if (last < 0) return
  const current = hover.value ?? last
  let next: number | null = null
  switch (event.key) {
    case 'ArrowLeft':
      next = Math.max(0, current - 1)
      break
    case 'ArrowRight':
      next = Math.min(last, current + 1)
      break
    case 'Home':
      next = 0
      break
    case 'End':
      next = last
      break
    case 'Escape':
      hover.value = null
      announce.value = ''
      return
    default:
      return
  }
  event.preventDefault()
  hover.value = next
  announce.value = describe(next)
}

const tip = computed(() => {
  const index = hover.value
  if (index === null || index >= props.ts.length) return null
  const x = xs.value[index] as number
  const t = props.ts[index] as number
  const clock =
    props.endWallMs === null
      ? null
      : new Date(props.endWallMs - (tMax.value - t)).toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', second: '2-digit' })
  return {
    index,
    x,
    // Flip to the other side of the crosshair near the right edge, so the box never leaves the chart.
    flip: x > width.value * 0.6,
    when: formatAgo(tMax.value - t),
    clock,
    rows: props.series.map((s) => {
      const value = s.values[index]
      return {
        id: s.id,
        label: s.label,
        color: s.color,
        text: value === null || value === undefined ? '—' : s.format(value),
        y: value === null || value === undefined ? null : yScale.value(value),
      }
    }),
  }
})
</script>

<template>
  <div class="nl-chart">
    <ul v-if="series.length > 1" class="nl-chart-legend">
      <li v-for="s in series" :key="s.id">
        <span class="nl-key" :style="{ backgroundColor: s.color }" aria-hidden="true" />
        {{ s.label }}
      </li>
    </ul>

    <div
      ref="wrap"
      class="nl-chart-frame"
      role="group"
      tabindex="0"
      aria-roledescription="line chart"
      :aria-label="title"
      @pointermove="onPointer"
      @pointerdown="onPointer"
      @pointerleave="onLeave"
      @keydown="onKey"
      @blur="onLeave"
    >
      <svg :width="width" :height="height" :viewBox="`0 0 ${width} ${height}`" aria-hidden="true">
        <!-- Hairline grid, one step off the card surface; the baseline is a step stronger. -->
        <g class="nl-chart-grid">
          <line v-for="tick in yGrid" :key="tick.value" :x1="plot.x0" :x2="plot.x1" :y1="tick.y" :y2="tick.y" />
        </g>
        <line class="nl-chart-axis" :x1="plot.x0" :x2="plot.x1" :y1="plot.y1" :y2="plot.y1" />
        <g class="nl-chart-ticks">
          <text v-for="tick in yGrid" :key="tick.value" :x="plot.x0 - 8" :y="tick.y" text-anchor="end" dominant-baseline="middle">
            {{ axisFormat(tick.value) }}
          </text>
          <text
            v-for="tick in xGrid"
            :key="tick.offset"
            :x="tick.x"
            :y="plot.y1 + 18"
            :text-anchor="tick.offset === 0 ? 'end' : 'middle'"
          >
            {{ tick.label }}
          </text>
        </g>

        <g v-if="limit && limitY !== null">
          <line class="nl-chart-limit" :x1="plot.x0" :x2="plot.x1" :y1="limitY" :y2="limitY" />
          <text class="nl-chart-limit-label" :x="plot.x0 + 6" :y="limitY - 6">{{ limit.label }}</text>
        </g>

        <g v-for="line in lines" :key="line.series.id">
          <path v-for="(d, i) in line.areas" :key="`a${i}`" :d="d" class="nl-chart-area" :style="{ fill: line.series.color }" />
          <path v-for="(d, i) in line.paths" :key="`l${i}`" :d="d" class="nl-chart-line" :style="{ stroke: line.series.color }" />
        </g>

        <g v-for="point in extremes" :key="point.kind" class="nl-chart-extreme">
          <circle :cx="point.x" :cy="point.y" r="4" :style="{ fill: lines[0]?.series.color }" />
          <text :x="point.x" :y="point.kind === 'max' ? point.y - 10 : point.y + 18" text-anchor="middle">
            {{ point.kind }} {{ point.text }}
          </text>
        </g>

        <g v-if="single && single.lastIndex >= 0">
          <circle
            class="nl-chart-end"
            :cx="xs[single.lastIndex]"
            :cy="single.ys[single.lastIndex]"
            r="4"
            :style="{ fill: single.series.color }"
          />
          <text
            class="nl-chart-end-label"
            :x="xs[single.lastIndex]"
            :y="(single.ys[single.lastIndex] as number) - 12"
            text-anchor="end"
          >
            {{ single.series.format(single.series.values[single.lastIndex] as number) }}
          </text>
        </g>

        <g v-if="tip">
          <line class="nl-chart-cursor" :x1="tip.x" :x2="tip.x" :y1="plot.y0" :y2="plot.y1" />
          <template v-for="row in tip.rows" :key="row.id">
            <circle v-if="row.y !== null" class="nl-chart-end" :cx="tip.x" :cy="row.y" r="4" :style="{ fill: row.color }" />
          </template>
        </g>
      </svg>

      <div v-if="tip" class="nl-chart-tip" :style="tip.flip ? { right: `${width - tip.x + 14}px` } : { left: `${tip.x + 14}px` }">
        <p class="nl-chart-tip-when">{{ tip.when }}<template v-if="tip.clock"> · {{ tip.clock }}</template></p>
        <p v-for="row in tip.rows" :key="row.id" class="nl-chart-tip-row">
          <span class="nl-key" :style="{ backgroundColor: row.color }" aria-hidden="true" />
          <strong>{{ row.text }}</strong>
          <span>{{ row.label }}</span>
        </p>
      </div>
    </div>

    <p class="nl-visually-hidden">{{ summary }}</p>
    <p class="nl-visually-hidden" role="status">{{ announce }}</p>
  </div>
</template>
