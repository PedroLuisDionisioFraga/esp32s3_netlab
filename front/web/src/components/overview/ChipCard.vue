<script setup lang="ts">
import type { SystemInfo } from '../../api'
import { formatBytes, formatUptime } from '../../format'
import NlCard from '../../ui/NlCard.vue'
import NlCardHead from '../../ui/NlCardHead.vue'

defineProps<{ info: SystemInfo | null }>()
</script>

<template>
  <NlCard>
    <template #head>
      <NlCardHead title="Chip" subtitle="The ESP32-S3 itself" />
    </template>
    <p v-if="!info" class="nl-hint">Loading…</p>
    <dl v-else class="nl-dl">
      <dt>Chip</dt>
      <dd>{{ info.chip }} · {{ info.cores }} cores</dd>
      <dt>ESP-IDF</dt>
      <dd class="nl-mono">{{ info.idf_version }}</dd>
      <dt>Uptime</dt>
      <dd class="nl-num">{{ formatUptime(info.uptime_s) }}</dd>
      <dt>Last reset</dt>
      <dd>{{ info.reset_reason }}</dd>
      <dt>Free memory</dt>
      <dd class="nl-num">{{ formatBytes(info.free_heap) }}</dd>
      <dt>Lowest free</dt>
      <dd class="nl-num">{{ formatBytes(info.min_free_heap) }}</dd>
    </dl>
  </NlCard>
</template>
