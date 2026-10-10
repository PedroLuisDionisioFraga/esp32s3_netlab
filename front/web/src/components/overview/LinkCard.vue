<script setup lang="ts">
import { computed } from 'vue'
import { RouterLink } from 'vue-router'
import type { LinkInfo } from '../../api'
import { signalQuality } from '../../format'
import NlCard from '../../ui/NlCard.vue'
import NlCardHead from '../../ui/NlCardHead.vue'
import NlPill from '../../ui/NlPill.vue'

// The Wi-Fi link to the router. On the Network page it is the full card; on the Overview it links there.
const props = defineProps<{ link: LinkInfo | null; withLink?: boolean }>()

const quality = computed(() => (props.link?.associated && props.link.rssi !== undefined ? signalQuality(props.link.rssi) : null))
</script>

<template>
  <NlCard>
    <template #head>
      <NlCardHead title="Wi-Fi link" subtitle="How the lab reaches your router">
        <template #actions>
          <NlPill v-if="link" :tone="link.associated ? (link.online ? 'success' : 'warning') : 'error'">
            {{ link.associated ? (link.online ? 'Online' : 'Waiting for DHCP') : 'Not connected' }}
          </NlPill>
        </template>
      </NlCardHead>
    </template>

    <p v-if="!link" class="nl-hint">Loading…</p>
    <p v-else-if="!link.associated" class="nl-notice nl-notice-error">
      Not connected to the router. The lab keeps retrying every two seconds.
    </p>
    <template v-else>
      <div v-if="quality" class="nl-cluster" style="margin-bottom: 14px">
        <span class="nl-signal" aria-hidden="true">
          <i v-for="n in 4" :key="n" :class="{ 'is-on': n <= quality.bars }" />
        </span>
        <strong>{{ quality.label }}</strong>
        <span class="nl-hint nl-num">{{ link.rssi }} dBm</span>
      </div>
      <dl class="nl-dl">
        <dt>Network</dt>
        <dd>{{ link.ssid }}</dd>
        <dt>Channel</dt>
        <dd class="nl-num">{{ link.channel }}</dd>
        <dt>Router BSSID</dt>
        <dd class="nl-mono">{{ link.bssid }}</dd>
        <dt>IP address</dt>
        <dd class="nl-mono">{{ link.online ? link.ip : 'waiting for DHCP…' }}</dd>
        <template v-if="link.online">
          <dt>Gateway</dt>
          <dd class="nl-mono">{{ link.gateway }}</dd>
          <dt>Netmask</dt>
          <dd class="nl-mono">{{ link.netmask }}</dd>
        </template>
      </dl>
    </template>

    <template v-if="withLink" #footer>
      <RouterLink to="/network" class="nl-btn nl-btn-neutral">Manage the network</RouterLink>
    </template>
  </NlCard>
</template>
