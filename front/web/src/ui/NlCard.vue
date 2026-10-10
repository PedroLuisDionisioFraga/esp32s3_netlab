<script setup lang="ts">
withDefaults(
  defineProps<{
    /** No body padding and clipped corners: a table fills the card to its edges. */
    flush?: boolean
    as?: 'article' | 'section' | 'div'
    /** Work in flight: a sliding bar under the head and aria-busy. */
    busy?: boolean
  }>(),
  { as: 'section' },
)

defineSlots<{ head?(): unknown; default?(): unknown; footer?(): unknown }>()
</script>

<template>
  <component :is="as" class="nl-card" :class="{ 'is-flush': flush }" :aria-busy="busy ? 'true' : undefined">
    <slot name="head" />
    <div v-if="busy" class="nl-progress" aria-hidden="true"><span /></div>
    <div class="nl-card-body"><slot /></div>
    <div v-if="$slots.footer" class="nl-card-foot"><slot name="footer" /></div>
  </component>
</template>
