<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  label: string
  value?: number | string | null
  unit?: string
  sub?: string
  /** A small note beside the label; `down` is drawn in the error colour, `up` in the success one. */
  delta?: { text: string; trend: 'up' | 'down' | 'flat' }
  loading?: boolean
  /** Fixed decimals for a numeric value. */
  fractionDigits?: number
}>()

defineSlots<{ sub?(): unknown; spark?(): unknown }>()

const display = computed(() => {
  if (props.loading || props.value === null || props.value === undefined || props.value === '') return '—'
  if (typeof props.value === 'number') {
    return props.value.toLocaleString('en-US', {
      minimumFractionDigits: props.fractionDigits,
      maximumFractionDigits: props.fractionDigits ?? 2,
    })
  }
  return props.value
})
</script>

<template>
  <div class="nl-kpi" :aria-busy="loading ? 'true' : undefined">
    <div class="nl-kpi-head">
      <span class="nl-kpi-label">{{ label }}</span>
      <span v-if="delta" class="nl-kpi-delta" :class="`is-${delta.trend}`">{{ delta.text }}</span>
    </div>
    <div class="nl-kpi-value">
      {{ display }}<span v-if="unit && display !== '—'" class="nl-kpi-unit">{{ unit }}</span>
    </div>
    <div v-if="sub || $slots.sub" class="nl-kpi-sub">
      <slot name="sub">{{ sub }}</slot>
    </div>
    <div v-if="$slots.spark" class="nl-kpi-spark">
      <slot name="spark" />
    </div>
  </div>
</template>
