import { formatBytes, formatPercent10 } from './format'

// Memory monitor: what the controller sends and what the page makes of it. The firmware side is
// components/services/memory_service on top of heaptop: one JSON document in heaptop's stream-protocol names, sizes
// in bytes, `*10` fields percent x 10, `null` for a data source that is off or a region that does not exist.
// Pure (no Vue, no fetch), so Vitest can load it; the requests are in memory-api.ts.

/** Bits of `MemorySnapshot.features`: which data sources the firmware was built with. */
export const FEATURE_RUNTIME_STATS = 1 << 0
export const FEATURE_TASK_HEAP = 1 << 1
export const FEATURE_FAIL_CB = 1 << 2

export type MemoryAlert = 'dram_free' | 'dram_largest' | 'frag' | 'psram_free' | 'stack' | 'leak' | 'alloc_fail'

export interface MemoryRegion {
  total: number
  free: number
  /** Lowest free size since boot, or since the last clear. */
  min: number
  /** Largest block that could be allocated now. */
  largest: number
  /** 0 = one contiguous free block, 1000 = fully fragmented. */
  frag10: number
  used_blocks: number
  free_blocks: number
}

export type MemoryTaskState = 'running' | 'ready' | 'blocked' | 'suspended' | 'deleted' | 'unknown'

export interface MemoryTask {
  name: string
  state: MemoryTaskState
  prio: number
  core: number | null
  /** Percent x10 of one core over the last interval; null without run-time stats. */
  cpu10: number | null
  /** Lowest free stack ever, in bytes. */
  hwm: number
  /** Heap held now, its peak, the part in PSRAM and its change over the history window, in bytes. Only with task
   * tracking (FEATURE_TASK_HEAP), which the SBC leaves off. */
  heap?: number
  peak?: number
  psram?: number
  growth?: number
  /** heaptop thinks the task leaks. */
  leak?: boolean
}

/** The alert limits heaptop runs with; a limit of 0 turns that check off. */
export interface MemoryLimits {
  dram_free_min: number
  dram_largest_min: number
  frag_pct_max: number
  psram_free_min: number
  stack_hwm_min: number
  task_growth: number
}

export interface MemorySnapshot {
  ht: number
  /** Sample number; grows by one per sample. */
  seq: number
  t_ms: number
  /** Uptime of the last clear; 0 = the statistics run since boot. */
  since_ms: number
  period_ms: number
  dt_ms: number
  /** Time heaptop spent taking this sample. */
  self_us: number
  features: number
  cpu10: number[] | null
  regions: { internal: MemoryRegion | null; dma: MemoryRegion | null; psram: MemoryRegion | null }
  failures: number | null
  alerts: MemoryAlert[]
  limits?: MemoryLimits
  /** Oldest sample first. */
  trend?: { len: number; internal_free: number[]; internal_largest: number[]; psram_free?: number[] }
  tasks?: MemoryTask[]
  tasks_truncated?: boolean
}

export interface MemoryClearResult {
  ok: boolean
  /** The sampler was slow: the clear still applies on a later sample. */
  pending?: boolean
}

export const TASK_STATE_LABELS: Readonly<Record<MemoryTaskState, string>> = {
  running: 'Running',
  ready: 'Ready',
  blocked: 'Blocked',
  suspended: 'Suspended',
  deleted: 'Deleted',
  unknown: 'Unknown',
}

export const ALERT_LABELS: Readonly<Record<MemoryAlert, string>> = {
  dram_free: 'Free memory',
  dram_largest: 'Largest free block',
  frag: 'Fragmentation',
  psram_free: 'PSRAM free',
  stack: 'Task stacks',
  leak: 'Per-task leaks',
  alloc_fail: 'Allocation failures',
}

/** 'heap' needs task tracking (FEATURE_TASK_HEAP); a snapshot without it sorts every task as equal on that key. */
export type TaskSort = 'cpu' | 'stack' | 'name' | 'heap'

/** A copy of `tasks` in the order asked for: busiest first, tightest stack first, or alphabetical. */
export function sortTasks(tasks: readonly MemoryTask[], sort: TaskSort): MemoryTask[] {
  const byName = (a: MemoryTask, b: MemoryTask) => a.name.localeCompare(b.name)
  const copy = [...tasks]
  switch (sort) {
    case 'cpu':
      // A task without a CPU reading (run-time stats off) sorts last.
      return copy.sort((a, b) => (b.cpu10 ?? -1) - (a.cpu10 ?? -1) || byName(a, b))
    case 'stack':
      return copy.sort((a, b) => a.hwm - b.hwm || byName(a, b))
    case 'heap':
      return copy.sort((a, b) => (b.heap ?? -1) - (a.heap ?? -1) || byName(a, b))
    case 'name':
      return copy.sort(byName)
  }
}

/** What narrows the task table. Every field is "no filter" at its default. */
export interface TaskFilters {
  /** Part of the name, any case. */
  query: string
  /** States to show; empty = every state. */
  states: MemoryTaskState[]
  /** Only tasks under heaptop's stack floor. */
  onlyTight: boolean
  /** Only tasks that used some CPU in the last interval. */
  onlyActive: boolean
  /** Only tasks on this core; null = any. */
  core: number | null
  /** Only tasks heaptop suspects of leaking (task tracking). */
  onlyLeaks: boolean
}

export const NO_TASK_FILTERS: Readonly<TaskFilters> = {
  query: '',
  states: [],
  onlyTight: false,
  onlyActive: false,
  core: null,
  onlyLeaks: false,
}

/** `tasks` narrowed by `filters`, in the same order. `stackFloor` is heaptop's `stack_hwm_min` (0 = check off). */
export function filterTasks(tasks: readonly MemoryTask[], filters: TaskFilters, stackFloor: number): MemoryTask[] {
  const query = filters.query.trim().toLowerCase()
  return tasks.filter(
    (task) =>
      (query === '' || task.name.toLowerCase().includes(query)) &&
      (filters.states.length === 0 || filters.states.includes(task.state)) &&
      (!filters.onlyTight || (stackFloor > 0 && task.hwm < stackFloor)) &&
      (!filters.onlyActive || (task.cpu10 !== null && task.cpu10 > 0)) &&
      (filters.core === null || task.core === filters.core) &&
      (!filters.onlyLeaks || task.leak === true),
  )
}

/** How many tasks are in each state, for the counts on the state chips. */
export function countByState(tasks: readonly MemoryTask[]): Record<MemoryTaskState, number> {
  const counts: Record<MemoryTaskState, number> = { running: 0, ready: 0, blocked: 0, suspended: 0, deleted: 0, unknown: 0 }
  for (const task of tasks) counts[task.state] += 1
  return counts
}

/** How many filters are on, for the "Limpar filtros" button. */
export function activeFilterCount(filters: TaskFilters): number {
  return (
    (filters.query.trim() === '' ? 0 : 1) +
    (filters.states.length > 0 ? 1 : 0) +
    (filters.onlyTight ? 1 : 0) +
    (filters.onlyActive ? 1 : 0) +
    (filters.core === null ? 0 : 1) +
    (filters.onlyLeaks ? 1 : 0)
  )
}

/**
 * A key per task that survives sorting and filtering, so rows keep their DOM between polls: the name, plus the
 * occurrence number among tasks of the same name, in the order the snapshot lists them.
 */
export function taskKeys(tasks: readonly MemoryTask[]): Map<MemoryTask, string> {
  const seen = new Map<string, number>()
  const keys = new Map<MemoryTask, string>()
  for (const task of tasks) {
    const occurrence = (seen.get(task.name) ?? 0) + 1
    seen.set(task.name, occurrence)
    keys.set(task, `${task.name}#${occurrence}`)
  }
  return keys
}

const KNOWN_STATES: readonly MemoryTaskState[] = ['running', 'ready', 'blocked', 'suspended', 'deleted', 'unknown']

/** Filters read back from storage: anything that is not the right shape falls back to "no filter" for that field. */
export function parseTaskFilters(raw: string | null): TaskFilters {
  const filters: TaskFilters = { ...NO_TASK_FILTERS, states: [] }
  if (raw === null) return filters
  let value: unknown
  try {
    value = JSON.parse(raw)
  } catch {
    return filters
  }
  if (typeof value !== 'object' || value === null) return filters
  const stored = value as Record<string, unknown>
  if (typeof stored.query === 'string') filters.query = stored.query.slice(0, 64)
  if (Array.isArray(stored.states)) {
    filters.states = stored.states.filter((state): state is MemoryTaskState => KNOWN_STATES.includes(state as MemoryTaskState))
  }
  if (typeof stored.onlyTight === 'boolean') filters.onlyTight = stored.onlyTight
  if (typeof stored.onlyActive === 'boolean') filters.onlyActive = stored.onlyActive
  if (typeof stored.core === 'number' && Number.isInteger(stored.core) && stored.core >= 0) filters.core = stored.core
  if (typeof stored.onlyLeaks === 'boolean') filters.onlyLeaks = stored.onlyLeaks
  return filters
}

const FILTERS_KEY = 'netlab:task-filters'

/** The filters of this tab; storage can be missing or blocked (private window), then there are none. */
export function loadTaskFilters(): TaskFilters {
  try {
    return parseTaskFilters(sessionStorage.getItem(FILTERS_KEY))
  } catch {
    return parseTaskFilters(null)
  }
}

export function saveTaskFilters(filters: TaskFilters): void {
  try {
    sessionStorage.setItem(FILTERS_KEY, JSON.stringify(filters))
  } catch {
    /* a convenience only: the filters still work without it */
  }
}

/** Lowest stack high-water mark among the tasks still alive, or null when there are none. */
export function lowestStack(tasks: readonly MemoryTask[] | undefined): number | null {
  const alive = (tasks ?? []).filter((task) => task.state !== 'deleted')
  return alive.length === 0 ? null : Math.min(...alive.map((task) => task.hwm))
}

/** 'unmeasured': heaptop has no data source for it here; 'off': its limit is 0. */
export type HealthState = 'ok' | 'alert' | 'off' | 'unmeasured'

export interface HealthRow {
  id: MemoryAlert
  label: string
  /** What was measured now, or why nothing was. */
  reading: string
  /** The limit it is held to. */
  limit: string
  state: HealthState
}

/** The seven heaptop checks, each with the value it looks at, its limit and whether it is firing. */
export function healthRows(snapshot: MemorySnapshot): HealthRow[] {
  const { limits, regions } = snapshot
  const fires = (id: MemoryAlert) => snapshot.alerts.includes(id)
  // A limit of 0 turns a check off; without limits (an older firmware) only the alert list is known.
  const stateOf = (id: MemoryAlert, limit: number | undefined): HealthState =>
    limit === 0 ? 'off' : fires(id) ? 'alert' : 'ok'
  const floor = (limit: number | undefined, show: (value: number) => string) =>
    limit === undefined ? '—' : limit === 0 ? 'Off' : `min ${show(limit)}`

  const internal = regions.internal
  const psram = regions.psram
  const stack = lowestStack(snapshot.tasks)
  const hasFailures = (snapshot.features & FEATURE_FAIL_CB) !== 0 && snapshot.failures !== null
  const hasLeakCheck = (snapshot.features & FEATURE_TASK_HEAP) !== 0

  return [
    {
      id: 'dram_free',
      label: ALERT_LABELS.dram_free,
      reading: internal ? formatBytes(internal.free) : '—',
      limit: floor(limits?.dram_free_min, formatBytes),
      state: stateOf('dram_free', limits?.dram_free_min),
    },
    {
      id: 'dram_largest',
      label: ALERT_LABELS.dram_largest,
      reading: internal ? formatBytes(internal.largest) : '—',
      limit: floor(limits?.dram_largest_min, formatBytes),
      state: stateOf('dram_largest', limits?.dram_largest_min),
    },
    {
      id: 'frag',
      label: ALERT_LABELS.frag,
      reading: internal ? formatPercent10(internal.frag10) : '—',
      limit:
        limits === undefined ? '—' : limits.frag_pct_max === 0 ? 'Off' : `max ${limits.frag_pct_max} %`,
      state: stateOf('frag', limits?.frag_pct_max),
    },
    psram
      ? {
          id: 'psram_free',
          label: ALERT_LABELS.psram_free,
          reading: formatBytes(psram.free),
          limit: floor(limits?.psram_free_min, formatBytes),
          state: stateOf('psram_free', limits?.psram_free_min),
        }
      : {
          id: 'psram_free',
          label: ALERT_LABELS.psram_free,
          reading: 'No PSRAM on this device',
          limit: '—',
          state: 'unmeasured',
        },
    {
      id: 'stack',
      label: ALERT_LABELS.stack,
      reading: stack === null ? '—' : `${formatBytes(stack)} free on the tightest`,
      limit: floor(limits?.stack_hwm_min, formatBytes),
      state: stateOf('stack', limits?.stack_hwm_min),
    },
    hasLeakCheck
      ? {
          id: 'leak',
          label: ALERT_LABELS.leak,
          reading: fires('leak') ? 'A task looks like it leaks' : 'No suspects',
          limit: limits === undefined ? '—' : `growth ≥ ${formatBytes(limits.task_growth)}`,
          state: stateOf('leak', limits?.task_growth),
        }
      : {
          id: 'leak',
          label: ALERT_LABELS.leak,
          reading: 'Per-task heap tracking is off',
          limit: '—',
          state: 'unmeasured',
        },
    hasFailures
      ? {
          id: 'alloc_fail',
          label: ALERT_LABELS.alloc_fail,
          reading: `${snapshot.failures} since the last clear`,
          limit: 'none new',
          state: fires('alloc_fail') ? 'alert' : 'ok',
        }
      : {
          id: 'alloc_fail',
          label: ALERT_LABELS.alloc_fail,
          reading: 'No failure log',
          limit: '—',
          state: 'unmeasured',
        },
  ]
}
