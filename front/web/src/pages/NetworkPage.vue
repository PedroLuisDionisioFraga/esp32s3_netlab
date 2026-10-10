<script setup lang="ts">
import { computed, ref } from 'vue'
import { describeError, forgetWifi, getLink, scanNetworks, type LinkInfo, type ScannedNetwork } from '../api'
import LoadNotice from '../components/LoadNotice.vue'
import LinkCard from '../components/overview/LinkCard.vue'
import { signalQuality } from '../format'
import NlCard from '../ui/NlCard.vue'
import NlCardHead from '../ui/NlCardHead.vue'
import NlIcon from '../ui/NlIcon.vue'
import NlPageHeader from '../ui/NlPageHeader.vue'
import NlPill from '../ui/NlPill.vue'
import { useConfirm } from '../ui/useConfirm'
import { useToast } from '../ui/useToast'
import { usePolling } from '../usePolling'

const { confirm } = useConfirm()
const { show } = useToast()

const link = ref<LinkInfo | null>(null)
const linkError = ref('')

async function loadLink(): Promise<void> {
  try {
    link.value = await getLink()
    linkError.value = ''
  } catch (error) {
    linkError.value = describeError(error)
    throw error
  }
}
const polling = usePolling(loadLink, 4000)

// ---- Scan ----
const networks = ref<ScannedNetwork[] | null>(null)
const scanning = ref(false)
const scanError = ref('')

// The strongest first; the one the lab is on is marked, wherever it is in the list.
const sorted = computed(() => [...(networks.value ?? [])].sort((a, b) => b.rssi - a.rssi))

async function scan(): Promise<void> {
  scanning.value = true
  scanError.value = ''
  try {
    networks.value = (await scanNetworks()).networks
  } catch (error) {
    scanError.value = describeError(error)
  } finally {
    scanning.value = false
  }
}

// ---- Forget the network ----
const restarting = ref(false)

async function forget(): Promise<void> {
  const ok = await confirm({
    title: 'Forget this Wi-Fi network?',
    message:
      'The lab restarts and opens its own setup Wi-Fi, where you can choose another network. This page stops working until it is back on a network.',
    confirmLabel: 'Forget and restart',
    tone: 'danger',
  })
  if (!ok) return
  try {
    await forgetWifi()
    restarting.value = true
    polling.stop()
  } catch (error) {
    show(`Could not forget the network: ${describeError(error)}`, { tone: 'error' })
  }
}
</script>

<template>
  <div class="nl-stack">
    <NlPageHeader title="Network" subtitle="The Wi-Fi the lab is on, and the networks around it" />

    <LoadNotice :loading="false" :error="linkError" what="the Wi-Fi link" :stale="link !== null" @retry="polling.stop(); polling.start()" />

    <p v-if="restarting" class="nl-notice nl-notice-warn" role="status">
      <NlIcon name="info" />
      <span>
        Restarting into setup mode. Join the Wi-Fi called “Netlab-Fraga” and open <span class="nl-mono">http://192.168.4.1/</span>.
      </span>
    </p>

    <div class="nl-grid-main">
      <NlCard flush>
        <template #head>
          <NlCardHead title="Networks in range" subtitle="A scan takes a few seconds and pauses the lab's Wi-Fi briefly">
            <template #actions>
              <button type="button" class="nl-btn nl-btn-primary" :disabled="scanning || restarting" @click="scan">
                <NlIcon name="refresh-cw" :class="{ 'nl-spin': scanning }" />
                {{ scanning ? 'Scanning…' : networks ? 'Scan again' : 'Scan' }}
              </button>
            </template>
          </NlCardHead>
        </template>

        <p v-if="scanError" class="nl-notice nl-notice-error" role="alert" style="margin: 16px">
          <NlIcon name="triangle-alert" /><span>{{ scanError }}</span>
        </p>
        <p v-else-if="networks === null" class="nl-hint" style="padding: 20px">Press Scan to list the networks around the lab.</p>
        <p v-else-if="networks.length === 0" class="nl-hint" style="padding: 20px">No networks found. The ESP32-S3 sees 2.4 GHz only.</p>
        <ul v-else class="nl-scan" aria-label="Networks in range">
          <li v-for="network in sorted" :key="`${network.ssid}-${network.channel}`">
            <span class="nl-signal" aria-hidden="true">
              <i v-for="n in 4" :key="n" :class="{ 'is-on': n <= signalQuality(network.rssi).bars }" />
            </span>
            <span class="nl-scan-name">
              {{ network.ssid || '(hidden network)' }}
              <NlPill v-if="link?.associated && link.ssid === network.ssid" tone="success">Connected</NlPill>
            </span>
            <span class="nl-scan-meta nl-num">ch {{ network.channel }} · {{ network.rssi }} dBm</span>
            <span class="nl-scan-meta">
              <NlIcon v-if="network.secure" name="lock" />
              <span class="nl-visually-hidden">{{ network.secure ? 'Secured' : 'Open' }}</span>
              <span v-if="!network.secure" aria-hidden="true">open</span>
            </span>
          </li>
        </ul>
      </NlCard>

      <div class="nl-stack">
        <LinkCard :link="link" />
        <NlCard>
          <template #head>
            <NlCardHead title="Change network" subtitle="Move the lab to another router" />
          </template>
          <p class="nl-hint" style="margin-bottom: 12px">
            Forgetting the network erases it from the lab's memory and restarts it into setup mode, like holding the BOOT
            button for five seconds.
          </p>
          <button type="button" class="nl-btn nl-btn-danger" :disabled="restarting" @click="forget">Forget this network…</button>
        </NlCard>
      </div>
    </div>
  </div>
</template>
