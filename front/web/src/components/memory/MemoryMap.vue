<script setup lang="ts">
import { computed } from 'vue'
import { formatBytes } from '../../format'

// One heap region as a bar split in three: what is in use, what is free but scattered in small pieces, and the one
// largest free block. The first and last are what an allocation can still count on; the middle is what fragmentation
// has cost. Segments are separated by a 2px gap of the card surface, never by a border.
const props = defineProps<{ total: number; free: number; largest: number }>()

const parts = computed(() => {
  const total = Math.max(props.total, 1)
  const free = Math.min(Math.max(props.free, 0), props.total)
  const largest = Math.min(Math.max(props.largest, 0), free)
  return [
    { key: 'used', label: 'In use', bytes: props.total - free, color: 'var(--nl-series-1)' },
    { key: 'scattered', label: 'Free, in small pieces', bytes: free - largest, color: 'var(--nl-map-scattered)' },
    { key: 'largest', label: 'Largest free block', bytes: largest, color: 'var(--nl-series-3)' },
  ].map((part) => ({ ...part, pct: (part.bytes / total) * 100 }))
})

const summary = computed(() => parts.value.map((part) => `${part.label}: ${formatBytes(part.bytes)}`).join('; '))
const pct = (value: number) => `${value.toLocaleString('en-US', { maximumFractionDigits: 1 })} %`
</script>

<template>
  <div class="nl-map">
    <div class="nl-map-bar" role="img" :aria-label="`Region map. ${summary}`">
      <span
        v-for="part in parts.filter((item) => item.bytes > 0)"
        :key="part.key"
        class="nl-map-seg"
        :style="{ flexGrow: part.bytes, backgroundColor: part.color }"
        :title="`${part.label}: ${formatBytes(part.bytes)} (${pct(part.pct)})`"
      />
    </div>
    <ul class="nl-map-legend">
      <li v-for="part in parts" :key="part.key">
        <span class="nl-key" :style="{ backgroundColor: part.color }" aria-hidden="true" />
        <span class="nl-map-label">{{ part.label }}</span>
        <strong class="num">{{ formatBytes(part.bytes) }}</strong>
        <span class="nl-map-pct num">{{ pct(part.pct) }}</span>
      </li>
    </ul>
  </div>
</template>
