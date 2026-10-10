<script setup lang="ts">
import type { PillTone } from './types'

// The dot repeats the status in a second channel: colour is never the only thing that says it.
withDefaults(defineProps<{ tone?: PillTone; dot?: boolean }>(), { tone: 'neutral', dot: true })
defineSlots<{ default(): unknown }>()
</script>

<template>
  <span class="nl-pill" :class="`nl-pill-${tone}`">
    <span v-if="dot" class="nl-pill-dot" aria-hidden="true" />
    <slot />
  </span>
</template>
