<script setup lang="ts">
import { computed, ref, toRef, useId } from 'vue'
import NlIcon from './NlIcon.vue'
import { useModalFocus } from './useModalFocus'

// A dialog centered over the page. The backdrop and Escape close it; the focus stays inside while it is open and goes
// back to `returnFocus` (the button that opened it) afterwards. The content is the default slot.
const props = defineProps<{
  open: boolean
  title: string
  subtitle?: string
  /** 780px wide instead of 440px: for a chart. */
  wide?: boolean
  returnFocus?: HTMLElement | null
}>()

const emit = defineEmits<{ close: [] }>()
defineSlots<{ default(): unknown; actions?(): unknown }>()

const titleId = useId()
const panel = ref<HTMLElement | null>(null)

const openModel = computed({
  get: () => props.open,
  set: (value: boolean) => {
    if (!value) emit('close')
  },
})
useModalFocus(openModel, panel, toRef(props, 'returnFocus'))
</script>

<template>
  <Teleport to="body">
    <Transition name="nl-fade">
      <div v-if="open" class="nl-dialog-layer">
        <div class="nl-dialog-backdrop" @click="emit('close')" />
        <div
          ref="panel"
          class="nl-dialog"
          :class="{ 'is-wide': wide }"
          role="dialog"
          aria-modal="true"
          :aria-labelledby="titleId"
          tabindex="-1"
        >
          <div class="nl-dialog-head">
            <div>
              <h2 :id="titleId">{{ title }}</h2>
              <p v-if="subtitle" class="nl-dialog-sub">{{ subtitle }}</p>
            </div>
            <div class="nl-cluster">
              <slot name="actions" />
              <button type="button" class="nl-icon-btn" data-autofocus aria-label="Close" @click="emit('close')">
                <NlIcon name="x" />
              </button>
            </div>
          </div>
          <slot />
        </div>
      </div>
    </Transition>
  </Teleport>
</template>
