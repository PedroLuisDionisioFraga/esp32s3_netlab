<script setup lang="ts">
import { computed, ref } from 'vue'
import { RouterLink } from 'vue-router'
import { describeError, getLink, getSystemInfo, type LinkInfo, type SystemInfo } from '../api'
import ChipCard from '../components/overview/ChipCard.vue'
import LedCard from '../components/overview/LedCard.vue'
import LinkCard from '../components/overview/LinkCard.vue'
import LoadNotice from '../components/LoadNotice.vue'
import { formatBytes, formatUptime, signalQuality } from '../format'
import NlIcon from '../ui/NlIcon.vue'
import NlKpiTile from '../ui/NlKpiTile.vue'
import NlPageHeader from '../ui/NlPageHeader.vue'
import { usePolling } from '../usePolling'

const HOT_C = 70 // the chip is warm from here

const info = ref<SystemInfo | null>(null)
const link = ref<LinkInfo | null>(null)
const infoError = ref('')
const linkError = ref('')
const loading = ref(false)

async function loadInfo(): Promise<void> {
  loading.value = true
  try {
    info.value = await getSystemInfo()
    infoError.value = ''
  } catch (error) {
    infoError.value = describeError(error)
    throw error
  } finally {
    loading.value = false
  }
}

async function loadLink(): Promise<void> {
  try {
    link.value = await getLink()
    linkError.value = ''
  } catch (error) {
    linkError.value = describeError(error)
    throw error
  }
}

const infoPolling = usePolling(loadInfo, 2000)
const linkPolling = usePolling(loadLink, 4000)

function retry(): void {
  for (const polling of [infoPolling, linkPolling]) {
    polling.stop()
    polling.start()
  }
}

const temperature = computed(() => info.value?.temperature_c ?? null)
const hot = computed(() => temperature.value !== null && temperature.value >= HOT_C)
const quality = computed(() => (link.value?.associated && link.value.rssi !== undefined ? signalQuality(link.value.rssi) : null))
const error = computed(() => infoError.value || linkError.value)
</script>

<template>
  <div class="nl-stack">
    <NlPageHeader title="Overview" subtitle="The chip, its Wi-Fi link and the onboard LED" />

    <LoadNotice :loading="loading && !info" :error="error" what="the lab" :stale="info !== null" @retry="retry" />

    <section class="nl-grid-kpi" aria-label="Key figures" :aria-busy="!info ? 'true' : undefined">
      <NlKpiTile
        label="Chip temperature"
        :value="temperature"
        unit="°C"
        :fraction-digits="1"
        :delta="hot ? { text: 'running hot', trend: 'down' } : undefined"
        sub="die temperature, not the room"
        :loading="!info"
      />
      <NlKpiTile label="Uptime" :value="info ? formatUptime(info.uptime_s) : null" sub="since the last reset" :loading="!info" />
      <NlKpiTile
        label="Free memory"
        :value="info ? formatBytes(info.free_heap) : null"
        :sub="info ? `lowest ${formatBytes(info.min_free_heap)}` : undefined"
        :loading="!info"
      >
        <template #sub>
          <template v-if="info">
            lowest {{ formatBytes(info.min_free_heap) }} ·
            <RouterLink to="/memory">details <NlIcon name="chevron-right" /></RouterLink>
          </template>
        </template>
      </NlKpiTile>
      <NlKpiTile
        label="Wi-Fi signal"
        :value="link?.associated && link.rssi !== undefined ? link.rssi : null"
        unit="dBm"
        :sub="quality ? quality.label : link && !link.associated ? 'not connected' : undefined"
        :loading="!link"
      />
    </section>

    <div class="nl-grid-cards">
      <ChipCard :info="info" />
      <LinkCard :link="link" with-link />
      <LedCard :led="info?.led ?? null" @changed="infoPolling.stop(); infoPolling.start()" />
    </div>
  </div>
</template>
