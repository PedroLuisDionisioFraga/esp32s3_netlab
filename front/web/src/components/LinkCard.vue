<script setup>
import { computed, ref } from 'vue'
import { forgetWifi } from '../api'
import { signalQuality } from '../format'

const props = defineProps({
  link: { type: Object, default: null },
  error: { type: String, default: null },
})

const quality = computed(() => (props.link?.associated ? signalQuality(props.link.rssi) : null))

const restarting = ref(false)
const forgetError = ref(null)

async function changeNetwork() {
  const ok = window.confirm(
    'Forget this Wi-Fi network? The device restarts and opens its own setup Wi-Fi, where you can choose another network.',
  )
  if (!ok) {
    return
  }
  forgetError.value = null
  try {
    await forgetWifi()
    restarting.value = true
  } catch (err) {
    forgetError.value = err.message
  }
}
</script>

<template>
  <section class="card">
    <h2>Wi-Fi link</h2>
    <p v-if="error" class="error">{{ error }}</p>

    <template v-if="link">
      <p v-if="!link.associated" class="error">Not connected to the router.</p>

      <template v-else>
        <div class="row">
          <span class="bars" aria-hidden="true">
            <i v-for="n in 4" :key="n" :class="{ on: n <= quality.bars }" />
          </span>
          <strong>{{ quality.label }}</strong>
          <span class="hint">{{ link.rssi }} dBm</span>
        </div>

        <dl>
          <dt>Network</dt>
          <dd>{{ link.ssid }}</dd>
          <dt>Channel</dt>
          <dd>{{ link.channel }}</dd>
          <dt>Router BSSID</dt>
          <dd>{{ link.bssid }}</dd>
          <template v-if="link.online">
            <dt>IP address</dt>
            <dd>{{ link.ip }}</dd>
            <dt>Gateway</dt>
            <dd>{{ link.gateway }}</dd>
            <dt>Netmask</dt>
            <dd>{{ link.netmask }}</dd>
          </template>
          <template v-else>
            <dt>IP address</dt>
            <dd>waiting for DHCP…</dd>
          </template>
        </dl>
      </template>

      <div class="row action">
        <button :disabled="restarting" @click="changeNetwork">Change network…</button>
      </div>
      <p v-if="restarting" class="hint">
        Restarting into setup mode. Join the Wi-Fi called “Netlab-Fraga” and open http://192.168.4.1/
      </p>
      <p v-if="forgetError" class="error">{{ forgetError }}</p>
    </template>
    <p v-else-if="!error" class="hint">Loading…</p>
  </section>
</template>
