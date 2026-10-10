<script setup lang="ts">
import { computed, nextTick, ref } from 'vue'
import type { SegmentedOption } from './types'

const props = defineProps<{
  options: readonly SegmentedOption[]
  /** The group's name, for a screen reader, e.g. "Time window". */
  label: string
}>()

const model = defineModel<string>({ required: true })
const root = ref<HTMLElement | null>(null)

// A radio group: one tab stop, the arrow keys move the selection (WAI-ARIA authoring practices).
const selectedIndex = computed(() => Math.max(0, props.options.findIndex((option) => option.value === model.value)))

function select(index: number, moveFocus: boolean): void {
  const option = props.options[index]
  if (!option) return
  model.value = option.value
  if (moveFocus) void nextTick(() => root.value?.querySelectorAll<HTMLElement>('[role="radio"]')[index]?.focus())
}

function onKeydown(event: KeyboardEvent, index: number): void {
  const last = props.options.length - 1
  let next: number
  switch (event.key) {
    case 'ArrowRight':
    case 'ArrowDown':
      next = index === last ? 0 : index + 1
      break
    case 'ArrowLeft':
    case 'ArrowUp':
      next = index === 0 ? last : index - 1
      break
    case 'Home':
      next = 0
      break
    case 'End':
      next = last
      break
    default:
      return
  }
  event.preventDefault()
  select(next, true)
}
</script>

<template>
  <div ref="root" class="nl-seg" role="radiogroup" :aria-label="label">
    <button
      v-for="(option, index) in options"
      :key="option.value"
      type="button"
      role="radio"
      :aria-checked="option.value === model ? 'true' : 'false'"
      :tabindex="index === selectedIndex ? 0 : -1"
      @click="select(index, false)"
      @keydown="onKeydown($event, index)"
    >
      {{ option.label }}
    </button>
  </div>
</template>
