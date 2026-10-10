<script setup lang="ts">
import NlIcon from '../ui/NlIcon.vue'

// A read that failed: what could not be read, why, and a way to retry. The last good data stays on the page
// (`stale`), so a hiccup does not blank a dashboard. The status region is always mounted and only its text changes
// (a region created together with its text is not announced).
defineProps<{ loading: boolean; error: string; what: string; stale?: boolean }>()
defineEmits<{ retry: [] }>()
</script>

<template>
  <p class="nl-visually-hidden" role="status">{{ loading ? 'Loading…' : '' }}</p>
  <div v-if="error" class="nl-notice nl-notice-error" role="alert">
    <NlIcon name="triangle-alert" />
    <div class="nl-stack nl-gap-8">
      <p>Could not read {{ what }}. {{ error }}</p>
      <p v-if="stale">Showing the last reading.</p>
      <div>
        <button type="button" class="nl-btn nl-btn-neutral" :disabled="loading" @click="$emit('retry')">Try again</button>
      </div>
    </div>
  </div>
</template>
