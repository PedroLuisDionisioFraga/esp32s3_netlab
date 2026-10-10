<script setup>
import { computed, ref } from 'vue'
import { clearCapture, getRouter, sendLab, setCapture } from '../api'
import { formatBytes, formatUptime } from '../format'
import { usePolling } from '../usePolling'

const PROTOCOLS = { 1: 'ICMP', 6: 'TCP', 17: 'UDP' }

const { data: router, error, refresh } = usePolling(getRouter, 3000)

const busy = ref(false)
const actionError = ref(null)
const labText = ref('')
const labNotice = ref(null)

// Most recently active first.
const flows = computed(() => [...(router.value?.flows ?? [])].sort((a, b) => b.last_s - a.last_s))

const ago = (seconds) => `${formatUptime(router.value.now_s - seconds)} ago`
const protocol = (proto) => PROTOCOLS[proto] ?? `IP ${proto}`

async function act(action) {
  busy.value = true
  actionError.value = null
  try {
    await action()
    refresh()
  } catch (err) {
    actionError.value = err.message
  } finally {
    busy.value = false
  }
}

const toggleCapture = () => act(() => setCapture(!router.value.capture))

function clearAll() {
  if (window.confirm('Forget every recorded flow, DNS name and lab service request, and zero the counters?')) {
    act(clearCapture)
  }
}

// What the device answers is the whole record (every table is bounded), so it is the export too.
function exportJson() {
  const blob = new Blob([JSON.stringify(router.value, null, 2)], { type: 'application/json' })
  const link = document.createElement('a')
  link.href = URL.createObjectURL(blob)
  link.download = `netlab-capture-${new Date().toISOString().replace(/[:.]/g, '-')}.json`
  link.click()
  URL.revokeObjectURL(link.href)
}

async function sendToLab() {
  labNotice.value = null
  try {
    const res = await sendLab(labText.value)
    labNotice.value = `The device received ${res.length} bytes.`
    labText.value = ''
    refresh()
  } catch (err) {
    labNotice.value = err.message
  }
}
</script>

<template>
  <!-- On an error the last answer stays on screen under the message. -->
  <p v-if="error" class="error card wide">{{ error }}</p>
  <p v-if="!router && !error" class="hint card wide">Loading…</p>

  <section v-if="router && !router.enabled" class="card wide">
    <h2>Router lab</h2>
    <p>This firmware is built without router lab mode.</p>
    <p class="hint">
      Enable it in <code>menuconfig</code> → <em>Wi-Fi bridge</em> → <em>Router lab mode</em>, then build and flash
      again. See the README for the lab rules first.
    </p>
  </section>

  <template v-else-if="router">
    <section class="card">
      <h2>Lab network</h2>
      <div class="big" :class="{ warm: !router.active }">{{ router.active ? 'Routing' : 'Closed' }}</div>
      <p v-if="router.problem" class="error">{{ router.problem }}</p>
      <p v-else-if="!router.active" class="hint">It opens once the device is online through the saved network.</p>
      <dl>
        <dt>Network</dt>
        <dd>{{ router.ssid }}</dd>
        <dt>Device address</dt>
        <dd>{{ router.ip }}</dd>
        <template v-if="router.active">
          <dt>Channel</dt>
          <dd>{{ router.channel }} (the uplink's)</dd>
        </template>
        <dt>Clients</dt>
        <dd>{{ router.clients.length }} of {{ router.max_clients }}</dd>
      </dl>
    </section>

    <section class="card">
      <h2>Capture</h2>
      <div class="big" :class="{ warm: router.capture }">{{ router.capture ? 'On' : 'Off' }}</div>
      <p class="hint">
        While on, the device records which addresses and DNS names lab clients reach, never what they send.
      </p>
      <dl>
        <dt>Sent by lab clients</dt>
        <dd>{{ router.packets }} packets, {{ formatBytes(router.bytes) }}</dd>
        <dt>Flows</dt>
        <dd>{{ router.flows.length }} of {{ router.flows_max }}</dd>
        <dt>Flows dropped (table full)</dt>
        <dd>{{ router.flows_dropped }}</dd>
      </dl>
      <div class="row action">
        <button :disabled="busy" :class="{ active: router.capture }" @click="toggleCapture">
          {{ router.capture ? 'Stop capture' : 'Start capture' }}
        </button>
        <button :disabled="busy" @click="clearAll">Clear…</button>
        <button @click="exportJson">Export JSON</button>
      </div>
      <p v-if="actionError" class="error">{{ actionError }}</p>
    </section>

    <section class="card wide">
      <h2>Clients</h2>
      <p v-if="!router.clients.length" class="hint">No device on the lab network.</p>
      <div v-else class="table-wrap">
        <table>
          <thead>
            <tr><th>MAC</th><th>Address</th><th>Signal</th></tr>
          </thead>
          <tbody>
            <tr v-for="c in router.clients" :key="c.mac">
              <td>{{ c.mac }}</td>
              <td>{{ c.ip ?? 'waiting for DHCP…' }}</td>
              <td>{{ c.rssi }} dBm</td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>

    <section class="card wide">
      <h2>Flows</h2>
      <p v-if="!flows.length" class="hint">
        {{ router.capture ? 'Nothing recorded yet.' : 'Capture is off.' }}
      </p>
      <div v-else class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Client</th><th>Destination</th><th>Port</th><th>Protocol</th>
              <th>Packets</th><th>Sent</th><th>Last seen</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="f in flows" :key="`${f.client}-${f.dst}-${f.port}-${f.proto}`">
              <td>{{ f.client }}</td>
              <td>{{ f.dst }}</td>
              <td>{{ f.port || '' }}</td>
              <td>{{ protocol(f.proto) }}</td>
              <td>{{ f.packets }}</td>
              <td>{{ formatBytes(f.bytes) }}</td>
              <td>{{ ago(f.last_s) }}</td>
            </tr>
          </tbody>
        </table>
      </div>
      <p class="hint">Counted from the client to the destination only. Replies are not counted.</p>
    </section>

    <section class="card wide">
      <h2>DNS names</h2>
      <p v-if="!router.dns.length" class="hint">
        {{ router.capture ? 'Nothing recorded yet.' : 'Capture is off.' }}
      </p>
      <div v-else class="table-wrap">
        <table>
          <thead>
            <tr><th>Client</th><th>Name</th><th>When</th></tr>
          </thead>
          <tbody>
            <tr v-for="(d, i) in router.dns" :key="i">
              <td>{{ d.client }}</td>
              <td>{{ d.name }}</td>
              <td>{{ ago(d.at_s) }}</td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>

    <section class="card wide">
      <h2>Lab service (plain HTTP)</h2>
      <p class="hint">
        Send text to the device's own HTTP test service: the table shows what the server received. Without TLS,
        anyone on the path reads it the same way. Never type a real password here.
      </p>
      <form class="row action" @submit.prevent="sendToLab">
        <input v-model="labText" class="lab-input" placeholder="hello from the lab" :disabled="!router.capture">
        <button type="submit" :disabled="!router.capture || !labText">Send</button>
      </form>
      <p v-if="!router.capture" class="hint">Start the capture to use it.</p>
      <p v-if="labNotice" class="hint">{{ labNotice }}</p>
      <div v-if="router.lab.length" class="table-wrap">
        <table>
          <thead>
            <tr><th>Client</th><th>When</th><th>Bytes</th><th>Start of the body</th></tr>
          </thead>
          <tbody>
            <tr v-for="(m, i) in router.lab" :key="i">
              <td>{{ m.client }}</td>
              <td>{{ ago(m.at_s) }}</td>
              <td>{{ m.length }}</td>
              <td class="mono">{{ m.text }}</td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>
  </template>
</template>
