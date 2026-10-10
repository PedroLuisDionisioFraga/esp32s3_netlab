<script setup lang="ts">
defineProps<{ title: string; subtitle?: string }>()
defineSlots<{ actions?(): unknown }>()
</script>

<template>
  <div class="nl-page-head">
    <div>
      <h1>{{ title }}</h1>
      <p v-if="subtitle" class="nl-page-sub">{{ subtitle }}</p>
    </div>
    <div v-if="$slots.actions" class="nl-cluster">
      <slot name="actions" />
    </div>
  </div>
</template>
