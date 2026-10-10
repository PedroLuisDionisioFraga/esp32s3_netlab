<script setup lang="ts">
withDefaults(
  defineProps<{
    title: string
    subtitle?: string
    /** Heading level: 2 under the page title, 3 inside a section. */
    level?: 2 | 3
  }>(),
  { level: 2 },
)

defineSlots<{ actions?(): unknown }>()
</script>

<template>
  <div class="nl-card-head">
    <div>
      <component :is="`h${level}`">{{ title }}</component>
      <p v-if="subtitle" class="nl-card-sub">{{ subtitle }}</p>
    </div>
    <div v-if="$slots.actions" class="nl-cluster">
      <slot name="actions" />
    </div>
  </div>
</template>
