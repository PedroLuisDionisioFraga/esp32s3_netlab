<script setup lang="ts">
import { computed } from 'vue'
import { formatBytes, formatPercent10 } from '../../format'
import type { MemorySnapshot } from '../../memory'
import NlCard from '../../ui/NlCard.vue'
import NlCardHead from '../../ui/NlCardHead.vue'

const props = defineProps<{ snapshot: MemorySnapshot }>()

const REGIONS = [
  ['internal', 'Internal RAM'],
  ['dma', 'DMA-capable RAM'],
  ['psram', 'PSRAM'],
] as const

// A region the chip does not have comes as null, so it is left out.
const regions = computed(() =>
  REGIONS.flatMap(([key, label]) => {
    const region = props.snapshot.regions[key]
    if (!region) return []
    const usedPct = region.total > 0 ? Math.round(((region.total - region.free) / region.total) * 100) : 0
    // The free-memory alert is about the internal RAM: its bar turns to the error colour with it.
    const alert = key === 'internal' && props.snapshot.alerts.includes('dram_free')
    return [{ key, label, region, usedPct, alert }]
  }),
)
</script>

<template>
  <NlCard>
    <template #head>
      <NlCardHead title="Regions" subtitle="DMA-capable RAM is a subset of the internal RAM" />
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
  </NlCard>
</template>
