<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { formatBytes, formatNumber, formatPercent10 } from '../../format'
import {
  FEATURE_RUNTIME_STATS,
  FEATURE_TASK_HEAP,
  NO_TASK_FILTERS,
  TASK_STATE_LABELS,
  activeFilterCount,
  countByState,
  filterTasks,
  loadTaskFilters,
  saveTaskFilters,
  sortTasks,
  taskKeys,
  type MemorySnapshot,
  type MemoryTaskState,
  type TaskFilters,
  type TaskSort,
} from '../../memory'
import NlCard from '../../ui/NlCard.vue'
import NlCardHead from '../../ui/NlCardHead.vue'
import NlDataTable from '../../ui/NlDataTable.vue'
import NlPill from '../../ui/NlPill.vue'
import NlSegmented from '../../ui/NlSegmented.vue'
import type { SegmentedOption, TableColumn } from '../../ui/types'

const props = defineProps<{ snapshot: MemorySnapshot }>()

// Filters belong to this tab (sessionStorage) and survive the 2 s polling: the table re-renders, they do not reset.
const filters = ref<TaskFilters>(loadTaskFilters())
watch(filters, (value) => saveTaskFilters(value), { deep: true })

const STATES: readonly MemoryTaskState[] = ['running', 'ready', 'blocked', 'suspended', 'deleted']

// Without run-time stats no task has a CPU reading; without task tracking none has heap figures.
const hasCpu = computed(() => (props.snapshot.features & FEATURE_RUNTIME_STATS) !== 0)
const hasHeap = computed(() => (props.snapshot.features & FEATURE_TASK_HEAP) !== 0)
const cores = computed(() => props.snapshot.cpu10?.length ?? 1)
const floor = computed(() => props.snapshot.limits?.stack_hwm_min ?? 0)

const tasks = computed(() => props.snapshot.tasks ?? [])
const keys = computed(() => taskKeys(tasks.value))
const counts = computed(() => countByState(tasks.value))
const activeCount = computed(() => activeFilterCount(filters.value))

// A state with no task is left off the chips, unless it is selected: then it stays so it can be turned off.
const stateChips = computed(() =>
  STATES.filter((state) => counts.value[state] > 0 || filters.value.states.includes(state)).map((state) => ({
    state,
    label: TASK_STATE_LABELS[state],
    count: counts.value[state],
    on: filters.value.states.includes(state),
  })),
)

function toggleState(state: MemoryTaskState): void {
  const current = filters.value.states
  filters.value.states = current.includes(state) ? current.filter((item) => item !== state) : [...current, state]
}

function setCore(event: Event): void {
  const value = (event.target as HTMLSelectElement).value
  filters.value.core = value === 'any' ? null : Number(value)
}

function clearFilters(): void {
  filters.value = { ...NO_TASK_FILTERS, states: [] }
}

// What the operator picked; busiest first until they pick something else.
const picked = ref<TaskSort>('cpu')
const sort = computed<TaskSort>(() => {
  if (picked.value === 'cpu' && !hasCpu.value) return 'stack'
  if (picked.value === 'heap' && !hasHeap.value) return hasCpu.value ? 'cpu' : 'stack'
  return picked.value
})

const sortOptions = computed<SegmentedOption[]>(() => [
  ...(hasCpu.value ? [{ value: 'cpu', label: 'CPU' }] : []),
  ...(hasHeap.value ? [{ value: 'heap', label: 'Heap' }] : []),
  { value: 'stack', label: 'Stack' },
  { value: 'name', label: 'Name' },
])

// NlSegmented binds a plain string, so the setter narrows what it hands back to a TaskSort.
const choice = computed<string>({
  get: () => sort.value,
  set: (value) => {
    if (value === 'cpu' || value === 'stack' || value === 'name' || value === 'heap') picked.value = value
  },
})

const shown = computed(() => sortTasks(filterTasks(tasks.value, filters.value, floor.value), sort.value))

const columns = computed<TableColumn[]>(() => [
  { key: 'name', label: 'Task', text: true },
  { key: 'state', label: 'State', text: true },
  { key: 'prio', label: 'Priority' },
  ...(cores.value > 1 ? [{ key: 'core', label: 'Core' }] : []),
  { key: 'hwm', label: 'Stack free (min)' },
  ...(hasCpu.value ? [{ key: 'cpu', label: 'CPU' }] : []),
  ...(hasHeap.value ? [{ key: 'heap', label: 'Heap' }, { key: 'peak', label: 'Peak' }] : []),
])

const rows = computed(() =>
  shown.value.map((task) => ({
    id: keys.value.get(task) ?? task.name,
    name: task.name,
    state: TASK_STATE_LABELS[task.state],
    prio: task.prio,
    core: task.core,
    hwm: formatBytes(task.hwm),
    // The stack check of heaptop looks at the tightest one, so mark the tasks under its floor.
    tight: floor.value > 0 && task.hwm < floor.value,
    cpu: task.cpu10 === null ? null : formatPercent10(task.cpu10),
    heap: task.heap === undefined ? null : formatBytes(task.heap),
    peak: task.peak === undefined ? null : formatBytes(task.peak),
    leak: task.leak === true,
  })),
)

const subtitle = computed(() => {
  const { period_ms: period, self_us: cost, tasks_truncated: truncated } = props.snapshot
  const base = `Sampled every ${formatNumber(period / 1000)} s · each reading costs ${formatNumber(cost / 1000)} ms`
  return truncated ? `${base} · there are more tasks than the monitor shows` : base
})

const emptyText = computed(() => (tasks.value.length === 0 ? 'No tasks in the sample.' : 'No task matches the filters.'))

// Spoken when a filter changes the list; the region is always mounted, only its text changes.
const resultText = computed(() => `${shown.value.length} of ${tasks.value.length} tasks`)
</script>

<template>
  <NlCard flush>
    <template #head>
      <NlCardHead title="Tasks" :subtitle="subtitle">
        <template #actions>
          <NlSegmented v-model="choice" :options="sortOptions" label="Sort tasks by" />
        </template>
      </NlCardHead>
    </template>

    <div class="nl-tasks-toolbar" role="search" aria-label="Filter tasks">
      <label class="nl-search">
        <input
          v-model="filters.query"
          class="nl-input"
          type="search"
          maxlength="64"
          autocomplete="off"
          placeholder="Filter by name"
          aria-label="Filter tasks by name"
        />
      </label>

      <div class="nl-tasks-chips">
        <button v-for="chip in stateChips" :key="chip.state" type="button" class="nl-chip" :aria-pressed="chip.on" @click="toggleState(chip.state)">
          {{ chip.label }} <span class="nl-chip-count nl-num">{{ chip.count }}</span>
        </button>
        <button v-if="floor > 0" type="button" class="nl-chip" :aria-pressed="filters.onlyTight" @click="filters.onlyTight = !filters.onlyTight">
          Low stack
        </button>
        <button v-if="hasCpu" type="button" class="nl-chip" :aria-pressed="filters.onlyActive" @click="filters.onlyActive = !filters.onlyActive">
          Using CPU
        </button>
        <button v-if="hasHeap" type="button" class="nl-chip" :aria-pressed="filters.onlyLeaks" @click="filters.onlyLeaks = !filters.onlyLeaks">
          Leak suspects
        </button>
        <select v-if="cores > 1" class="nl-select nl-core-select" aria-label="Filter by core" :value="filters.core ?? 'any'" @change="setCore">
          <option value="any">All cores</option>
          <option v-for="core in cores" :key="core" :value="core - 1">Core {{ core - 1 }}</option>
        </select>
      </div>

      <div class="nl-tasks-result">
        <span class="nl-num">{{ resultText }}</span>
        <button v-if="activeCount > 0" type="button" class="nl-btn nl-btn-link" @click="clearFilters">Clear filters ({{ activeCount }})</button>
      </div>
      <p class="nl-visually-hidden" role="status">{{ activeCount > 0 ? resultText : '' }}</p>
    </div>

    <NlDataTable :columns="columns" :rows="rows" row-key="id" caption="System tasks" :empty-text="emptyText">
      <template #cell-name="{ row, value }">
        <span>{{ value }}</span>
        <NlPill v-if="row.leak" tone="warning">Leak?</NlPill>
      </template>
      <template #cell-hwm="{ row, value }">
        <span class="nl-num">{{ value }}</span>
        <NlPill v-if="row.tight" tone="warning">Low</NlPill>
      </template>
    </NlDataTable>
  </NlCard>
</template>
