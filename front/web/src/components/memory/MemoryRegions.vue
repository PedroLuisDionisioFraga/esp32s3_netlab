<script setup lang="ts">
import { computed } from 'vue'
import { formatBytes, formatPercent10 } from '../../format'
import type { MemorySnapshot } from '../../memory'
import { STABILITY_WINDOW_MS, freeWindow, type HistoryPoint, type Stability } from '../../memory-history'
import NlCard from '../../ui/NlCard.vue'
import NlCardHead from '../../ui/NlCardHead.vue'
import NlPill from '../../ui/NlPill.vue'
import type { PillTone } from '../../ui/types'

const props = defineProps<{ snapshot: MemorySnapshot; points: readonly HistoryPoint[] }>()

const REGIONS = [
  ['internal', 'Internal RAM'],
  ['dma', 'DMA-capable RAM'],
  ['psram', 'PSRAM'],
] as const

const STABILITY_LABELS: Readonly<Record<Stability, string>> = {
  stable: 'Stable',
  rising: 'Rising',
  falling: 'Falling',
  varying: 'Varying',
  unknown: 'Too few readings',
}

const STABILITY_TONES: Readonly<Record<Stability, PillTone>> = {
  stable: 'success',
  rising: 'info',
  falling: 'warning',
  varying: 'neutral',
  unknown: 'neutral',
}

const windowText = `${STABILITY_WINDOW_MS / 60_000} min`

const signedBytes = (bytes: number) => `${bytes > 0 ? '+' : bytes < 0 ? '−' : ''}${formatBytes(Math.abs(bytes))}`

// A region the chip does not have comes as null, so it is left out.
const regions = computed(() =>
  REGIONS.flatMap(([key, label]) => {
    const region = props.snapshot.regions[key]
    if (!region) return []
    const usedPct = region.total > 0 ? Math.round(((region.total - region.free) / region.total) * 100) : 0
    // The free-memory alert is about the internal RAM: its bar turns to the error colour with it.
    const alert = key === 'internal' && props.snapshot.alerts.includes('dram_free')
    const recent = freeWindow(props.points, key)
    const range = recent
      ? formatBytes(recent.min) === formatBytes(recent.max)
        ? formatBytes(recent.min)
        : `${formatBytes(recent.min)} – ${formatBytes(recent.max)}`
      : '—'
    return [{ key, label, region, usedPct, alert, recent, range }]
  }),
)

// The internal region can be bigger than the DMA one: the chip's RTC fast RAM joins the heap but cannot do DMA.
const rtcBytes = computed(() => {
  const { internal, dma } = props.snapshot.regions
  return internal && dma ? Math.max(0, internal.total - dma.total) : 0
})

const subtitle = computed(() =>
  rtcBytes.value > 0
    ? `DMA-capable RAM is a subset of the internal RAM; the internal one also holds ${formatBytes(rtcBytes.value)} of RTC RAM, which has no DMA`
    : 'DMA-capable RAM is a subset of the internal RAM',
)
</script>

<template>
  <NlCard>
    <template #head>
      <NlCardHead title="Regions" :subtitle="subtitle" />
    </template>

    <div v-for="item in regions" :key="item.key" class="nl-region">
      <div class="nl-region-head">
        <h3>{{ item.label }}</h3>
        <span class="nl-num">{{ item.usedPct }} % in use</span>
      </div>
      <div
        class="nl-bar"
        :class="{ 'is-alert': item.alert }"
        role="progressbar"
        :aria-label="`${item.label} in use`"
        aria-valuemin="0"
        aria-valuemax="100"
        :aria-valuenow="item.usedPct"
      >
        <span :style="{ width: `${item.usedPct}%` }" />
      </div>
      <dl class="nl-dl">
        <dt>Total</dt>
        <dd class="nl-num">{{ formatBytes(item.region.total) }}</dd>
        <dt>Free now</dt>
        <dd class="nl-num">{{ formatBytes(item.region.free) }}</dd>
        <dt>Range over {{ windowText }}</dt>
        <dd class="nl-num">
          {{ item.range }}
          <template v-if="item.recent && item.recent.delta !== 0">
            · {{ signedBytes(item.recent.delta) }}
          </template>
        </dd>
        <dt>Behaviour</dt>
        <dd>
          <NlPill v-if="item.recent" :tone="STABILITY_TONES[item.recent.stability]">
            {{ STABILITY_LABELS[item.recent.stability] }}
          </NlPill>
          <span v-else class="nl-limit-text">Waiting for readings</span>
        </dd>
        <dt>Lowest free</dt>
        <dd class="nl-num">{{ formatBytes(item.region.min) }}</dd>
        <dt>Largest block</dt>
        <dd class="nl-num">{{ formatBytes(item.region.largest) }}</dd>
        <dt>Fragmentation</dt>
        <dd class="nl-num">{{ formatPercent10(item.region.frag10) }}</dd>
        <dt>Blocks</dt>
        <dd class="nl-num">{{ item.region.used_blocks }} in use · {{ item.region.free_blocks }} free</dd>
      </dl>
    </div>

    <p class="nl-limit-text">
      Standing still is expected: the firmware reserves almost all of its memory at boot and then allocates very little.
      What to worry about is a range that only goes down and never comes back.
    </p>
  </NlCard>
</template>
