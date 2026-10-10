<script setup lang="ts">
import { computed } from 'vue'

// A line with no axes, for a trend at a glance. The drawing keeps a fixed width (300) and takes its height from the
// prop, so it scales with the tile instead of being letterboxed.
const WIDTH = 300
const PAD = 5

const props = withDefaults(
  defineProps<{
    values: readonly number[]
    /** What the line shows, for a screen reader. */
    label: string
    height?: number
  }>(),
  { height: 42 },
)

const round = (value: number): number => Math.round(value * 10) / 10

const points = computed(() => {
  const values = props.values.filter((value) => Number.isFinite(value))
  if (values.length === 0) return []
  const lo = Math.min(...values)
  const hi = Math.max(...values)
  const usableX = WIDTH - PAD * 2
  const usableY = props.height - PAD * 2
  return values.map((value, index) => ({
    x: round(values.length === 1 ? WIDTH - PAD : PAD + (index / (values.length - 1)) * usableX),
    y: round(PAD + (1 - (hi === lo ? 0.5 : (value - lo) / (hi - lo))) * usableY),
  }))
})

const path = computed(() => points.value.map((point, i) => `${i === 0 ? 'M' : 'L'}${point.x} ${point.y}`).join(' '))
const last = computed(() => points.value[points.value.length - 1])
</script>

<template>
  <svg class="nl-sparkline" :viewBox="`0 0 ${WIDTH} ${height}`" role="img" :aria-label="label">
    <line class="nl-sparkline-base" x1="0" :y1="height - PAD" :x2="WIDTH" :y2="height - PAD" />
    <path v-if="points.length > 1" class="nl-sparkline-line" :d="path" />
    <circle v-if="last" class="nl-sparkline-last" :cx="last.x" :cy="last.y" r="4" />
  </svg>
</template>
