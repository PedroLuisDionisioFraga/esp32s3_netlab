<script setup lang="ts">
import type { LiveTone } from './types'

// "live" pulses; "warn" and "down" sit still: only a live source moves.
withDefaults(defineProps<{ tone?: LiveTone }>(), { tone: 'live' })
defineSlots<{ default(): unknown }>()
</script>

<template>
  <span class="nl-live" :class="{ 'is-warn': tone === 'warn', 'is-down': tone === 'down' }" role="status">
    <span class="nl-live-dot" aria-hidden="true" />
    <slot />
  </span>
</template>
