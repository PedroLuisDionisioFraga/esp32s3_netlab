<script setup lang="ts">
import { computed, ref, shallowRef, watch } from 'vue'
import { axisFor, formatAgo, type ChartLimit, type ChartSeries, type Sample } from '../../chart'
import { formatBytes, formatNumber, formatUptime } from '../../format'
import type { MemoryRegion, MemorySnapshot } from '../../memory'
import {
  HISTORY_RANGES,
  POLL_MS,
  formatMinutes,
  historyToCsv,
  minutesToLimit,
  seriesStats,
  sliceRange,
  type HistoryPoint,
  type HistoryRange,
  type MetricKind,
} from '../../memory-history'
import NlDataTable from '../../ui/NlDataTable.vue'
import NlIcon from '../../ui/NlIcon.vue'
import NlLiveBadge from '../../ui/NlLiveBadge.vue'
import NlModal from '../../ui/NlModal.vue'
import NlSegmented from '../../ui/NlSegmented.vue'
import type { SegmentedOption, TableColumn } from '../../ui/types'
import HistoryChart from './HistoryChart.vue'
import MemoryMap from './MemoryMap.vue'

// The detail of one KPI tile: a chart of its history, the numbers behind it, a trend and, per metric, what else
// explains it. Opens centered over the page, keeps updating while open, and can be paused to inspect a moment.
const props = defineProps<{
  kind: MetricKind | null
  snapshot: MemorySnapshot
  points: readonly HistoryPoint[]
  /** Wall-clock time (ms since the epoch) at which the newest point arrived. */
  endWallMs: number | null
  /** The tile that opened the modal: it gets the focus back on close. */
  returnFocus?: HTMLElement | null
}>()

const emit = defineEmits<{ close: [] }>()

const S1 = 'var(--nl-series-1)'
const S2 = 'var(--nl-series-2)'
const TABLE_ROWS = 300

const rangeKey = ref('5m')
const viewMode = ref('chart')
const includeLimit = ref(false)

// Pausing keeps a copy of what was on screen: the page goes on polling and remembering behind it.
interface Frozen {
  snapshot: MemorySnapshot
  points: readonly HistoryPoint[]
  endWallMs: number | null
}
const frozen = shallowRef<Frozen | null>(null)
const paused = computed(() => frozen.value !== null)
const current = computed<Frozen>(() => frozen.value ?? { snapshot: props.snapshot, points: props.points, endWallMs: props.endWallMs })

function togglePause(): void {
  frozen.value = frozen.value ? null : { snapshot: props.snapshot, points: props.points, endWallMs: props.endWallMs }
}

watch(
  () => props.kind,
  () => {
    frozen.value = null
    viewMode.value = 'chart'
    includeLimit.value = false
  },
)

const rangeOptions: SegmentedOption[] = HISTORY_RANGES.map((range) => ({ value: range.key, label: range.label }))
const viewOptions: SegmentedOption[] = [
  { value: 'chart', label: 'Chart' },
  { value: 'table', label: 'Table' },
]

const range = computed<HistoryRange>(
  () => HISTORY_RANGES.find((item) => item.key === rangeKey.value) ?? (HISTORY_RANGES[1] as HistoryRange),
)
const windowPoints = computed(() => sliceRange(current.value.points, range.value.ms))
const ts = computed(() => windowPoints.value.map((point) => point.t))

const pct = (value: number) => `${formatNumber(value)} %`
const signedBytes = (value: number) => `${value > 0 ? '+' : value < 0 ? '−' : ''}${formatBytes(Math.abs(value))}`
const signedPct = (value: number) => `${value > 0 ? '+' : value < 0 ? '−' : ''}${pct(Math.abs(value))}`

const cores = computed(() =>
  windowPoints.value.reduce((most, point) => Math.max(most, point.cpu10.length), current.value.snapshot.cpu10?.length ?? 0),
)

interface Config {
  title: string
  subtitle: string
  explain: string
  series: ChartSeries[]
  /** What the stat cards and the trend are computed from. */
  basis: Sample[]
  unit: number
  axisFormat: (value: number) => string
  domain?: readonly [number, number]
  limit: ChartLimit | null
  limitKind: 'floor' | 'ceiling'
  format: (value: number) => string
  signed: (value: number) => string
  /** A change per minute, as text. */
  rate: (perMin: number) => string
  /** The region the memory map draws, for the metrics that are about one. */
  region: MemoryRegion | null
}

const config = computed<Config | null>(() => {
  const limits = current.value.snapshot.limits
  const points = windowPoints.value
  const regions = current.value.snapshot.regions
  const bytesRate = (perMin: number) => `${formatBytes(Math.abs(perMin))}/min`

  switch (props.kind) {
    case 'free': {
      const values = points.map((point) => point.internal.free)
      const floor = limits?.dram_free_min ?? 0
      return {
        title: 'Free memory',
        subtitle: 'Internal RAM',
        explain:
          'The internal RAM that can still be allocated. A line that only falls and never comes back is usually a leak; ups and downs that repeat are normal use.',
        series: [{ id: 'free', label: 'Free memory', color: S1, values, area: true, format: formatBytes }],
        basis: values,
        unit: 1024,
        axisFormat: formatBytes,
        limit: floor > 0 ? { value: floor, label: `Limit ${formatBytes(floor)}` } : null,
        limitKind: 'floor',
        format: formatBytes,
        signed: signedBytes,
        rate: bytesRate,
        region: regions.internal,
      }
    }
    case 'largest': {
      const largest = points.map((point) => point.internal.largest)
      const free = points.map((point) => point.internal.free)
      const floor = limits?.dram_largest_min ?? 0
      return {
        title: 'Largest free block',
        subtitle: 'Internal RAM',
        explain:
          'The biggest contiguous piece that can still be allocated. Even with plenty of free memory, a buffer larger than this block fails.',
        series: [
          { id: 'largest', label: 'Largest free block', color: S1, values: largest, format: formatBytes },
          { id: 'free', label: 'Total free memory', color: S2, values: free, format: formatBytes },
        ],
        basis: largest,
        unit: 1024,
        axisFormat: formatBytes,
        limit: floor > 0 ? { value: floor, label: `Limit ${formatBytes(floor)}` } : null,
        limitKind: 'floor',
        format: formatBytes,
        signed: signedBytes,
        rate: bytesRate,
        region: regions.internal,
      }
    }
    case 'frag': {
      const values = points.map((point) => point.internal.frag10 / 10)
      const ceiling = limits?.frag_pct_max ?? 0
      return {
        title: 'Fragmentation',
        subtitle: 'Internal RAM',
        explain:
          'How much of the free memory is scattered in small pieces. 0 % is a single block; 100 % is everything chopped up, and no large allocation fits.',
        series: [{ id: 'frag', label: 'Fragmentation', color: S1, values, area: true, format: pct }],
        basis: values,
        unit: 1,
        axisFormat: (value) => `${value} %`,
        domain: [0, 100],
        limit: ceiling > 0 ? { value: ceiling, label: `Limit ${ceiling} %` } : null,
        limitKind: 'ceiling',
        format: pct,
        signed: signedPct,
        rate: (perMin) => `${pct(Math.abs(perMin))}/min`,
        region: regions.internal,
      }
    }
    case 'psram': {
      const free = points.map((point): Sample => point.psram?.free ?? null)
      const largest = points.map((point): Sample => point.psram?.largest ?? null)
      const floor = limits?.psram_free_min ?? 0
      return {
        title: 'PSRAM',
        subtitle: 'External RAM',
        explain:
          'The 8 MB of external RAM. heaptop keeps its own buffers here, and large allocations land here too, so a falling line deserves a look.',
        series: [
          { id: 'free', label: 'Free memory', color: S1, values: free, format: formatBytes },
          { id: 'largest', label: 'Largest free block', color: S2, values: largest, format: formatBytes },
        ],
        basis: free,
        unit: 1024,
        axisFormat: formatBytes,
        limit: floor > 0 ? { value: floor, label: `Limit ${formatBytes(floor)}` } : null,
        limitKind: 'floor',
        format: formatBytes,
        signed: signedBytes,
        rate: bytesRate,
        region: regions.psram,
      }
    }
    case 'cpu': {
      const perCore = Array.from({ length: Math.max(1, cores.value) }, (_, core) =>
        points.map((point): Sample => (point.cpu10[core] === undefined ? null : (point.cpu10[core] as number) / 10)),
      )
      const series: ChartSeries[] = perCore.map((values, core) => ({
        id: `core${core}`,
        label: perCore.length === 1 ? 'Processor' : `Core ${core}`,
        color: core === 0 ? S1 : S2,
        values,
        area: perCore.length === 1,
        format: pct,
      }))
      // The cards describe the whole chip: the mean of the cores at each sample.
      const basis = points.map((_, i): Sample => {
        const readings = perCore.map((values) => values[i]).filter((value): value is number => typeof value === 'number')
        return readings.length === 0 ? null : readings.reduce((sum, value) => sum + value, 0) / readings.length
      })
      return {
        title: 'CPU',
        subtitle: perCore.length === 1 ? 'Processor load' : `${perCore.length} cores`,
        explain:
          'Load measured from each core’s idle task: what it does not use, the other tasks used. The bars show who occupied the processor in the last reading.',
        series,
        basis,
        unit: 1,
        axisFormat: (value) => `${value} %`,
        domain: [0, 100],
        limit: null,
        limitKind: 'ceiling',
        format: pct,
        signed: signedPct,
        rate: (perMin) => `${pct(Math.abs(perMin))}/min`,
        region: null,
      }
    }
    default:
      return null
  }
})

const stats = computed(() => (config.value ? seriesStats(ts.value, config.value.basis) : null))

const cards = computed(() => {
  const c = config.value
  const s = stats.value
  if (!c || !s) return []
  const snap = current.value.snapshot
  const list = [
    { label: 'Now', value: c.format(s.last) },
    { label: 'Minimum', value: c.format(s.min) },
    { label: 'Maximum', value: c.format(s.max) },
    { label: 'Average', value: c.format(s.mean) },
    { label: 'Change', value: c.signed(s.delta) },
  ]
  const region = c.region
  if ((props.kind === 'free' || props.kind === 'psram') && region) {
    list.push({ label: snap.since_ms > 0 ? 'Lowest since clear' : 'Lowest since boot', value: formatBytes(region.min) })
  }
  if ((props.kind === 'largest' || props.kind === 'psram') && region && region.free > 0) {
    list.push({ label: 'Fits in one block', value: pct((region.largest / region.free) * 100) })
  }
  if (props.kind === 'cpu') {
    list.push({ label: 'Idle now', value: pct(Math.max(0, 100 - s.last)) })
    list.push({ label: 'Sample cost', value: `${formatNumber(snap.self_us / 1000)} ms` })
  }
  return list
})

// Whether the limit sits inside the y range the data alone would give: if not, offer to stretch the axis to it.
const limitOffScale = computed(() => {
  const c = config.value
  if (!c?.limit) return false
  const axis = axisFor(
    c.series.flatMap((series) => series.values),
    { fixed: c.domain, unit: c.unit },
  )
  return c.limit.value < axis.min || c.limit.value > axis.max
})

const insight = computed(() => {
  const c = config.value
  const s = stats.value
  if (!c || !s || s.count < 10 || s.slopePerMin === null) return null
  if (s.r2 === null) return { text: 'Trend: steady.', projection: null }
  if (s.r2 < 0.5) return { text: 'No clear trend: the readings wobble around one value.', projection: null }
  const arrow = s.slopePerMin > 0 ? '↗ rising' : s.slopePerMin < 0 ? '↘ falling' : 'steady'
  const minutes = c.limit ? minutesToLimit(s, c.limit.value, c.limitKind) : null
  return {
    text: `Trend: ${arrow} ${c.rate(s.slopePerMin)}.`,
    projection:
      minutes === null
        ? null
        : `At this pace it reaches the limit in ${formatMinutes(minutes)}. This is an estimate from a straight-line fit over ${s.count} readings, not a forecast.`,
  }
})

const windowText = computed(() => {
  if (ts.value.length === 0) return 'no readings'
  const span = (ts.value[ts.value.length - 1] as number) - (ts.value[0] as number)
  return `${ts.value.length} readings over ${formatUptime(span / 1000)}`
})

const summary = computed(() => {
  const c = config.value
  const s = stats.value
  if (!c || !s) return ''
  return `${c.title}: now ${c.format(s.last)}, minimum ${c.format(s.min)}, maximum ${c.format(s.max)}, average ${c.format(s.mean)}, ${windowText.value}.`
})

const subtitle = computed(() => {
  const c = config.value
  const every = formatNumber(current.value.snapshot.period_ms / 1000)
  return c ? `${c.subtitle} · heaptop samples every ${every} s` : ''
})

const tableColumns = computed<TableColumn[]>(() => [
  { key: 'when', label: 'When', text: true },
  ...(config.value?.series ?? []).map((series) => ({ key: series.id, label: series.label })),
])

const tableRows = computed(() => {
  const c = config.value
  if (!c) return []
  const end = ts.value[ts.value.length - 1] ?? 0
  const wall = current.value.endWallMs
  const rows: Record<string, unknown>[] = []
  for (let i = ts.value.length - 1; i >= 0 && rows.length < TABLE_ROWS; i--) {
    const t = ts.value[i] as number
    const clock =
      wall === null ? '' : ` · ${new Date(wall - (end - t)).toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit', second: '2-digit' })}`
    const row: Record<string, unknown> = { id: t, when: `${formatAgo(end - t)}${clock}` }
    for (const series of c.series) {
      const value = series.values[i]
      row[series.id] = value === null || value === undefined ? null : series.format(value)
    }
    rows.push(row)
  }
  return rows
})

const topTasks = computed(() =>
  (current.value.snapshot.tasks ?? [])
    .filter((task) => task.cpu10 !== null && task.cpu10 > 0)
    .sort((a, b) => (b.cpu10 as number) - (a.cpu10 as number))
    .slice(0, 5)
    .map((task) => ({ key: `${task.name}-${task.core ?? 'x'}`, name: task.name, core: task.core, value: (task.cpu10 as number) / 10 })),
)

function download(): void {
  const blob = new Blob([historyToCsv(windowPoints.value)], { type: 'text/csv;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = `memory-${new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-')}.csv`
  document.body.appendChild(link)
  link.click()
  link.remove()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
</script>

<template>
  <NlModal :open="kind !== null && config !== null" :title="config?.title ?? ''" :subtitle="subtitle" wide :return-focus="returnFocus" @close="emit('close')">
    <template #actions>
      <NlLiveBadge :tone="paused ? 'warn' : 'live'">{{ paused ? 'Paused' : 'Live' }}</NlLiveBadge>
    </template>

    <template v-if="config">
      <p class="nl-metric-text">{{ config.explain }}</p>

      <div class="nl-metric-bar">
        <NlSegmented v-model="rangeKey" :options="rangeOptions" label="Time window" />
        <NlSegmented v-model="viewMode" :options="viewOptions" label="Show as" />
        <button v-if="config.limit" type="button" class="nl-chip" :aria-pressed="includeLimit" @click="includeLimit = !includeLimit">
          {{ includeLimit ? 'Limit on the scale' : limitOffScale ? 'Limit is off the scale: include it' : 'Include the limit on the scale' }}
        </button>
        <span class="nl-metric-window nl-num">{{ windowText }}</span>
      </div>

      <div class="nl-metric-plot">
        <p v-if="ts.length < 2" class="nl-notice" role="status">
          <NlIcon name="info" /><span>Collecting readings: the chart appears after a couple of samples.</span>
        </p>
        <HistoryChart
          v-else-if="viewMode === 'chart'"
          :ts="ts"
          :series="config.series"
          :title="`History of ${config.title}`"
          :summary="summary"
          :unit="config.unit"
          :axis-format="config.axisFormat"
          :domain="config.domain"
          :limit="config.limit"
          :include-limit="includeLimit"
          :period-ms="POLL_MS"
          :span-ms="range.ms"
          :end-wall-ms="current.endWallMs"
        />
        <NlDataTable
          v-else
          :columns="tableColumns"
          :rows="tableRows"
          row-key="id"
          :caption="`Readings of ${config.title}, newest first`"
          empty-text="No readings yet."
        />
      </div>
      <p v-if="viewMode === 'table' && ts.length > TABLE_ROWS" class="nl-dialog-sub">
        Showing the {{ TABLE_ROWS }} newest readings; the CSV has every one of the window.
      </p>

      <dl class="nl-stats">
        <div v-for="card in cards" :key="card.label" class="nl-stat">
          <dt>{{ card.label }}</dt>
          <dd class="nl-num">{{ card.value }}</dd>
        </div>
      </dl>

      <div v-if="insight" class="nl-stack nl-gap-8">
        <p class="nl-metric-trend">{{ insight.text }}</p>
        <p v-if="insight.projection" class="nl-notice nl-notice-warn" role="status">
          <NlIcon name="triangle-alert" /><span>{{ insight.projection }}</span>
        </p>
      </div>

      <section v-if="config.region" class="nl-metric-extra">
        <h3>{{ kind === 'psram' ? 'PSRAM map' : 'Internal RAM map' }}</h3>
        <MemoryMap :total="config.region.total" :free="config.region.free" :largest="config.region.largest" />
        <p v-if="kind === 'frag'" class="nl-dialog-sub nl-num" style="margin-top: 8px">
          {{ config.region.used_blocks }} blocks in use · {{ config.region.free_blocks }} free blocks
        </p>
      </section>

      <section v-if="kind === 'cpu'" class="nl-metric-extra">
        <h3>Who used the processor just now</h3>
        <p v-if="topTasks.length === 0" class="nl-dialog-sub">No task used CPU in the last reading.</p>
        <ul v-else class="nl-top">
          <li v-for="task in topTasks" :key="task.key">
            <span class="nl-top-name">{{ task.name }}<template v-if="task.core !== null && cores > 1"> · core {{ task.core }}</template></span>
            <span class="nl-top-bar" aria-hidden="true"><span :style="{ width: `${Math.min(100, task.value)}%` }" /></span>
            <strong class="nl-num">{{ pct(task.value) }}</strong>
          </li>
        </ul>
      </section>

      <div class="nl-metric-foot">
        <button type="button" class="nl-btn nl-btn-neutral" :aria-pressed="paused" @click="togglePause">
          {{ paused ? 'Resume' : 'Pause chart' }}
        </button>
        <button type="button" class="nl-btn nl-btn-neutral" :disabled="windowPoints.length === 0" @click="download">Download CSV</button>
      </div>
    </template>
  </NlModal>
</template>
