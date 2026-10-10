<script setup lang="ts">
import { computed } from 'vue'
import NlIcon from './NlIcon.vue'
import { dismissToast, pauseToast, resumeToast, toastState } from './useToast'

// One toast at a time, at the bottom. Two live regions that are always mounted (a region that is created together
// with its text is not announced): errors are an alert, the rest are polite. The toast never takes the focus.
const icon = computed(() => {
  switch (toastState.current?.tone) {
    case 'success':
      return 'circle-check'
    case 'error':
      return 'triangle-alert'
    default:
      return 'info'
  }
})
</script>

<template>
  <div class="nl-toast-host">
    <Transition name="nl-toast">
      <div
        v-if="toastState.current"
        :key="toastState.current.id"
        class="nl-toast"
        :class="`nl-toast-${toastState.current.tone}`"
        @mouseenter="pauseToast"
        @mouseleave="resumeToast"
        @focusin="pauseToast"
        @focusout="resumeToast"
      >
        <NlIcon :name="icon" />
        <span>{{ toastState.current.message }}</span>
        <button type="button" class="nl-icon-btn" aria-label="Dismiss" @click="dismissToast">
          <NlIcon name="x" />
        </button>
      </div>
    </Transition>
    <p class="nl-visually-hidden" role="status">
      {{ toastState.current && toastState.current.tone !== 'error' ? toastState.current.message : '' }}
    </p>
    <p class="nl-visually-hidden" role="alert">
      {{ toastState.current && toastState.current.tone === 'error' ? toastState.current.message : '' }}
    </p>
  </div>
</template>
