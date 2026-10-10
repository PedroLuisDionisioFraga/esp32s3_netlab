<script setup lang="ts">
import { ICONS, type IconName } from './icons'

// The markup comes from the constant table in icons.ts (never from data), so v-html is safe here.
defineProps<{ name: IconName }>()
</script>

<template>
  <svg
    class="nl-icon"
    viewBox="0 0 24 24"
    fill="none"
    stroke="currentColor"
    stroke-width="2"
    stroke-linecap="round"
    stroke-linejoin="round"
    aria-hidden="true"
    focusable="false"
    v-html="ICONS[name]"
  />
</template>
