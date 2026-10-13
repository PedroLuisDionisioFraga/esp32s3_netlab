<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import LoadNotice from '../components/LoadNotice.vue'
import MemoryHealth from '../components/memory/MemoryHealth.vue'
import MemoryRegions from '../components/memory/MemoryRegions.vue'
import MemoryStorage from '../components/memory/MemoryStorage.vue'
import MemoryTasks from '../components/memory/MemoryTasks.vue'
import MetricModal from '../components/memory/MetricModal.vue'
import { describeError } from '../api'
import { ApiError } from '../http'
import { formatBytes, formatUptime } from '../format'
import { FEATURE_RUNTIME_STATS, fragRegion, type MemoryAlert, type MemorySnapshot } from '../memory'
import { clearMemory, fetchMemory } from '../memory-api'
import { POLL_MS, type MetricKind } from '../memory-history'
import type { StorageSnapshot } from '../storage'
import { STORAGE_POLL_MS, fetchStorage } from '../storage-api'
import NlIcon from '../ui/NlIcon.vue'
import NlKpiTile from '../ui/NlKpiTile.vue'
import NlLiveBadge from '../ui/NlLiveBadge.vue'
import NlPageHeader from '../ui/NlPageHeader.vue'
import NlSparkline from '../ui/NlSparkline.vue'
import { useToast } from '../ui/useToast'
import { useMemoryHistory } from '../useMemoryHistory'
import { usePolling } from '../usePolling'

const snapshot = ref<MemorySnapshot | null>(null)
const failure = ref('')
const loading = ref(false)
const clearing = ref(false)
const { show } = useToast()

async function load(): Promise<void> {
  loading.value = true
  try {
    snapshot.value = await fetchMemory()
    failure.value = ''
  } catch (error) {
    failure.value = describeError(error)
    throw error
  } finally {
    loading.value = false
  }
}

// heaptop samples once per second (components/heap_monitor); asking more often only repeats it.
const polling = usePolling(load, POLL_MS)

// Flash and NVS change slowly and are read on request (not sampled), so they have their own, slower poll. A firmware
// without the route (404) just has no card.
const storage = ref<StorageSnapshot | null>(null)
const storageFailure = ref('')
const storageMissing = ref(false)

async function loadStorage(): Promise<void> {
  try {
    storage.value = await fetchStorage()
    storageFailure.value = ''
  } catch (error) {
    if (error instanceof ApiError && error.status === 404) {
      storageMissing.value = true
      storageFailure.value = ''
      return
    }
    storageFailure.value = describeError(error)
    throw error
  }
}

const storagePolling = usePolling(loadStorage, STORAGE_POLL_MS)
watch(storageMissing, (gone) => {
  if (gone) storagePolling.stop()
})

// What the tile modals chart. It starts from heaptop's own 40-sample trend and grows with every poll.
const history = useMemoryHistory(snapshot)

// The tile whose modal is open, and the button that opened it (the focus goes back there on close).
const metric = ref<MetricKind | null>(null)
const opener = ref<HTMLElement | null>(null)

function openMetric(kind: MetricKind, event: Event): void {
  opener.value = event.currentTarget instanceof HTMLElement ? event.currentTarget : null
  metric.value = kind
}

function refresh(): void {
  storageMissing.value = false
  polling.stop()
  polling.start()
  storagePolling.stop()
  storagePolling.start()
}

async function clear(): Promise<void> {
  clearing.value = true
  try {
    const result = await clearMemory()
    show(result.pending ? 'Statistics cleared; they show on the next reading.' : 'Statistics cleared.', { tone: 'success' })
    // heaptop's windows (and its trend) start over, so the charts do too.
    history.reset()
    refresh()
  } catch (error) {
    show(`Could not clear the statistics: ${describeError(error)}`, { tone: 'error' })
  } finally {
    clearing.value = false
  }
}

const ready = computed(() => snapshot.value !== null)
const internal = computed(() => snapshot.value?.regions.internal ?? null)
/** The region the Fragmentation tile reads: the DMA one, so the RTC fast RAM does not inflate it (see fragRegion). */
const fragmentation = computed(() => (snapshot.value ? fragRegion(snapshot.value.regions) : null))
const psram = computed(() => snapshot.value?.regions.psram ?? null)
const limits = computed(() => snapshot.value?.limits)
const trend = computed(() => snapshot.value?.trend)

const kib = (bytes: number | undefined) => (bytes === undefined ? null : bytes / 1024)
/** A tile's delta turns red while the heaptop check it stands for is firing. */
const trendOf = (id: MemoryAlert): 'down' | 'flat' => (snapshot.value?.alerts.includes(id) ? 'down' : 'flat')

const freeDelta = computed(() =>
  internal.value ? { text: `min ${formatBytes(internal.value.min)}`, trend: trendOf('dram_free') } : undefined,
)
const largestDelta = computed(() =>
  limits.value && limits.value.dram_largest_min > 0
    ? { text: `limit ${formatBytes(limits.value.dram_largest_min)}`, trend: trendOf('dram_largest') }
    : undefined,
)
// The red state is heaptop's own `frag` alert, which it evaluates on the internal region, not on the one shown here.
const fragDelta = computed(() =>
  limits.value && limits.value.frag_pct_max > 0
    ? { text: `max ${limits.value.frag_pct_max} %`, trend: trendOf('frag') }
    : undefined,
)
const psramDelta = computed(() =>
  psram.value ? { text: `min ${formatBytes(psram.value.min)}`, trend: trendOf('psram_free') } : undefined,
)

/** Mean load of the cores, in percent; null while run-time stats are off. */
const cpu = computed(() => {
  const current = snapshot.value
  if (!current || (current.features & FEATURE_RUNTIME_STATS) === 0 || !current.cpu10 || current.cpu10.length === 0) return null
  return current.cpu10.reduce((sum, value) => sum + value, 0) / current.cpu10.length / 10
})

const subtitle = computed(() => {
  const current = snapshot.value
  if (!current) return 'Heap, fragmentation and tasks of the lab'
  const window = formatUptime(Math.max(0, current.t_ms - current.since_ms) / 1000)
  return `Statistics of ${window}, since ${current.since_ms > 0 ? 'the last clear' : 'boot'}`
})
</script>

<template>
  <div class="nl-stack">
    <NlPageHeader title="Memory" :subtitle="subtitle">
      <template #actions>
        <NlLiveBadge v-if="ready" :tone="failure ? 'down' : 'live'">{{ failure ? 'No connection' : 'Live' }}</NlLiveBadge>
        <button
          type="button"
          class="nl-btn nl-btn-neutral"
          :disabled="!ready || clearing"
          :aria-busy="clearing ? 'true' : undefined"
          @click="clear"
        >
          {{ clearing ? 'Clearing…' : 'Clear statistics' }}
        </button>
        <button type="button" class="nl-btn nl-btn-neutral" :disabled="loading" :aria-busy="loading ? 'true' : undefined" @click="refresh">
          <NlIcon name="refresh-cw" :class="{ 'nl-spin': loading }" />
          {{ loading ? 'Refreshing…' : 'Refresh' }}
        </button>
      </template>
    </NlPageHeader>

    <LoadNotice :loading="loading && !ready" :error="failure" what="the memory of the lab" :stale="ready" @retry="refresh" />

    <!-- Each tile is also a button (a stretched, transparent one: a block cannot sit inside a <button>). -->
    <section class="nl-grid-kpi" aria-label="Memory indicators" :aria-busy="!ready ? 'true' : undefined">
      <div class="nl-tile">
        <NlKpiTile
          label="Free memory"
          :value="kib(internal?.free)"
          unit="KiB"
          :fraction-digits="1"
          :delta="freeDelta"
          sub="internal RAM available now"
          :loading="!ready"
        >
          <template v-if="trend && trend.internal_free.length > 1" #spark>
            <NlSparkline :values="trend.internal_free" :label="`Free memory over the last ${trend.len} readings`" />
          </template>
        </NlKpiTile>
        <button v-if="ready" type="button" class="nl-tile-hit" aria-label="Show details of Free memory" aria-haspopup="dialog" @click="openMetric('free', $event)">
          <NlIcon name="chevron-right" />
        </button>
      </div>

      <div class="nl-tile">
        <NlKpiTile
          label="Largest free block"
          :value="kib(internal?.largest)"
          unit="KiB"
          :fraction-digits="1"
          :delta="largestDelta"
          sub="the biggest allocation that fits now"
          :loading="!ready"
        >
          <template v-if="trend && trend.internal_largest.length > 1" #spark>
            <NlSparkline :values="trend.internal_largest" :label="`Largest free block over the last ${trend.len} readings`" />
          </template>
        </NlKpiTile>
        <button v-if="ready" type="button" class="nl-tile-hit" aria-label="Show details of Largest free block" aria-haspopup="dialog" @click="openMetric('largest', $event)">
          <NlIcon name="chevron-right" />
        </button>
      </div>

      <div class="nl-tile">
        <NlKpiTile
          label="Fragmentation"
          :value="fragmentation ? fragmentation.frag10 / 10 : null"
          unit="%"
          :fraction-digits="1"
          :delta="fragDelta"
          :sub="snapshot?.regions.dma ? 'DMA-capable RAM · 0 % is a single free block' : '0 % is a single free block'"
          :loading="!ready"
        />
        <button v-if="ready" type="button" class="nl-tile-hit" aria-label="Show details of Fragmentation" aria-haspopup="dialog" @click="openMetric('frag', $event)">
          <NlIcon name="chevron-right" />
        </button>
      </div>

      <div v-if="!ready || psram" class="nl-tile">
        <NlKpiTile
          label="PSRAM free"
          :value="kib(psram?.free)"
          unit="KiB"
          :fraction-digits="0"
          :delta="psramDelta"
          sub="external RAM available now"
          :loading="!ready"
        >
          <template v-if="trend?.psram_free && trend.psram_free.length > 1" #spark>
            <NlSparkline :values="trend.psram_free" :label="`PSRAM free over the last ${trend.len} readings`" />
          </template>
        </NlKpiTile>
        <button v-if="ready" type="button" class="nl-tile-hit" aria-label="Show details of PSRAM" aria-haspopup="dialog" @click="openMetric('psram', $event)">
          <NlIcon name="chevron-right" />
        </button>
      </div>

      <div class="nl-tile">
        <NlKpiTile
          label="CPU"
          :value="cpu"
          unit="%"
          :fraction-digits="1"
          :sub="ready && cpu === null ? 'run-time stats are off' : 'mean load of the cores'"
          :loading="!ready"
        />
        <!-- Without run-time stats there is nothing to chart, so this tile stays a plain tile. -->
        <button v-if="ready && cpu !== null" type="button" class="nl-tile-hit" aria-label="Show details of CPU" aria-haspopup="dialog" @click="openMetric('cpu', $event)">
          <NlIcon name="chevron-right" />
        </button>
      </div>
    </section>

    <template v-if="snapshot">
      <div class="nl-grid-main">
        <MemoryRegions :snapshot="snapshot" :points="history.points.value" />
        <MemoryHealth :snapshot="snapshot" />
      </div>
      <LoadNotice
        v-if="storageFailure"
        :loading="false"
        :error="storageFailure"
        what="the storage of the lab"
        :stale="storage !== null"
        @retry="refresh"
      />
      <MemoryStorage v-if="storage" :storage="storage" />
      <MemoryTasks :snapshot="snapshot" />
      <MetricModal
        :kind="metric"
        :snapshot="snapshot"
        :points="history.points.value"
        :end-wall-ms="history.endWallMs.value"
        :return-focus="opener"
        @close="metric = null"
      />
    </template>
  </div>
</template>
