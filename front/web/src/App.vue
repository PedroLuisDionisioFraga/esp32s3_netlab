<script setup>
import { computed } from 'vue'
import { getLink, getSystemInfo } from './api'
import DeviceCard from './components/DeviceCard.vue'
import LedCard from './components/LedCard.vue'
import LinkCard from './components/LinkCard.vue'
import { usePolling } from './usePolling'

const { data: info, error: infoError, refresh: refreshInfo } = usePolling(getSystemInfo, 2000)
const { data: link, error: linkError } = usePolling(getLink, 4000)

const reachable = computed(() => info.value !== null && infoError.value === null)
</script>

<template>
  <header class="topbar">
    <h1>Netlab</h1>
    <span class="badge" :class="reachable ? 'ok' : 'bad'">
      {{ reachable ? 'Device reachable' : 'Device unreachable' }}
    </span>
  </header>

  <main class="grid">
    <DeviceCard :info="info" :error="infoError" />
    <LinkCard :link="link" :error="linkError" />
    <LedCard :led="info?.led ?? null" @changed="refreshInfo" />
  </main>
</template>
