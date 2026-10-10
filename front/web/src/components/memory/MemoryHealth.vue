<script setup lang="ts">
import { computed } from 'vue'
import { healthRows, type HealthState, type MemorySnapshot } from '../../memory'
import NlCard from '../../ui/NlCard.vue'
import NlCardHead from '../../ui/NlCardHead.vue'
import NlPill from '../../ui/NlPill.vue'
import type { PillTone } from '../../ui/types'

const props = defineProps<{ snapshot: MemorySnapshot }>()

const TONES: Readonly<Record<HealthState, PillTone>> = {
  ok: 'success',
  alert: 'error',
  off: 'neutral',
  unmeasured: 'neutral',
}

const LABELS: Readonly<Record<HealthState, string>> = {
  ok: 'Normal',
  alert: 'Alert',
  off: 'Off',
  unmeasured: 'Not measured',
}

const rows = computed(() => healthRows(props.snapshot))
const firing = computed(() => rows.value.filter((row) => row.state === 'alert').length)
</script>

<template>
  <NlCard>
    <template #head>
      <NlCardHead title="Health" subtitle="The checks heaptop runs on every sample">
        <template #actions>
          <NlPill :tone="firing > 0 ? 'error' : 'success'">
            {{ firing > 0 ? `${firing} in alert` : 'All normal' }}
          </NlPill>
        </template>
      </NlCardHead>
    </template>

    <ul class="nl-checks">
      <li v-for="row in rows" :key="row.id" class="nl-check-row">
        <div class="nl-check-text">
          <span class="nl-check-name">{{ row.label }}</span>
          <span class="nl-num">{{ row.reading }}</span>
          <span class="nl-limit-text">{{ row.limit }}</span>
        </div>
        <NlPill :tone="TONES[row.state]">{{ LABELS[row.state] }}</NlPill>
      </li>
    </ul>
  </NlCard>
</template>
