<script setup>
import { computed } from 'vue'
import { formatKiB, formatUptime } from '../format'

const props = defineProps({
  info: { type: Object, default: null },
  error: { type: String, default: null },
})

const temperature = computed(() => props.info?.temperature_c ?? null)
</script>

<template>
  <section class="card">
    <h2>Chip</h2>
    <p v-if="error" class="error">{{ error }}</p>

    <template v-if="info">
      <div class="big" :class="{ warm: temperature !== null && temperature >= 70 }">
        {{ temperature === null ? 'n/a' : `${temperature.toFixed(1)} °C` }}
      </div>
      <p class="hint">Die temperature of the chip itself, not the room.</p>

      <dl>
        <dt>Uptime</dt>
        <dd>{{ formatUptime(info.uptime_s) }}</dd>
        <dt>Free heap</dt>
        <dd>{{ formatKiB(info.free_heap) }}</dd>
        <dt>Lowest free heap</dt>
        <dd>{{ formatKiB(info.min_free_heap) }}</dd>
        <dt>Last reset</dt>
        <dd>{{ info.reset_reason }}</dd>
        <dt>Chip</dt>
        <dd>{{ info.chip }} ({{ info.cores }} cores)</dd>
        <dt>ESP-IDF</dt>
        <dd>{{ info.idf_version }}</dd>
      </dl>
    </template>
    <p v-else-if="!error" class="hint">Loading…</p>
  </section>
</template>
