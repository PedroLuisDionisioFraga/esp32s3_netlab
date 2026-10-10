<script setup lang="ts">
import { computed, ref } from 'vue'
import { clearCapture, describeError, getRouter, sendLab, setCapture, type RouterLab } from '../api'
import LoadNotice from '../components/LoadNotice.vue'
import { formatBytes, formatUptime } from '../format'
import NlCard from '../ui/NlCard.vue'
import NlCardHead from '../ui/NlCardHead.vue'
import NlDataTable from '../ui/NlDataTable.vue'
import NlIcon from '../ui/NlIcon.vue'
import NlKpiTile from '../ui/NlKpiTile.vue'
import NlPageHeader from '../ui/NlPageHeader.vue'
import NlPill from '../ui/NlPill.vue'
import type { TableColumn } from '../ui/types'
import { useConfirm } from '../ui/useConfirm'
import { useToast } from '../ui/useToast'
import { usePolling } from '../usePolling'

const PROTOCOLS: Record<number, string> = { 1: 'ICMP', 6: 'TCP', 17: 'UDP' }

const { confirm } = useConfirm()
const { show } = useToast()

// null until the first answer; `{ enabled: false }` when the firmware is built without router lab mode.
const state = ref<RouterLab | { enabled: false } | null>(null)
const error = ref('')

async function load(): Promise<void> {
  try {
    state.value = await getRouter()
    error.value = ''
  } catch (caught) {
    error.value = describeError(caught)
    throw caught
  }
}
const polling = usePolling(load, 3000)

const lab = computed<RouterLab | null>(() => (state.value?.enabled ? state.value : null))
const ago = (seconds: number): string => `${formatUptime((lab.value?.now_s ?? seconds) - seconds)} ago`

// ---- Tables ----
const clientColumns: TableColumn[] = [
  { key: 'mac', label: 'MAC', text: true },
  { key: 'ip', label: 'Address', text: true },
  { key: 'rssi', label: 'Signal' },
]
const clientRows = computed(() =>
  (lab.value?.clients ?? []).map((c) => ({ mac: c.mac, ip: c.ip ?? 'waiting for DHCP…', rssi: `${c.rssi} dBm` })),
)

const flowColumns: TableColumn[] = [
  { key: 'client', label: 'Client', text: true },
  { key: 'dst', label: 'Destination', text: true },
  { key: 'port', label: 'Port' },
  { key: 'proto', label: 'Protocol', text: true },
  { key: 'packets', label: 'Packets' },
  { key: 'sent', label: 'Sent' },
  { key: 'last', label: 'Last seen', text: true },
]
// Most recently active first.
const flowRows = computed(() =>
  [...(lab.value?.flows ?? [])]
    .sort((a, b) => b.last_s - a.last_s)
    .map((f) => ({
      id: `${f.client}-${f.dst}-${f.port}-${f.proto}`,
      client: f.client,
      dst: f.dst,
      port: f.port || null,
      proto: PROTOCOLS[f.proto] ?? `IP ${f.proto}`,
      packets: f.packets,
      sent: formatBytes(f.bytes),
      last: ago(f.last_s),
    })),
)

const dnsColumns: TableColumn[] = [
  { key: 'client', label: 'Client', text: true },
  { key: 'name', label: 'Name', text: true },
  { key: 'when', label: 'When', text: true },
]
const dnsRows = computed(() =>
  (lab.value?.dns ?? []).map((d, i) => ({ id: i, client: d.client, name: d.name, when: ago(d.at_s) })),
)

const labColumns: TableColumn[] = [
  { key: 'client', label: 'Client', text: true },
  { key: 'when', label: 'When', text: true },
  { key: 'length', label: 'Bytes' },
  { key: 'text', label: 'Start of the body', text: true },
]
const labRows = computed(() =>
  (lab.value?.lab ?? []).map((m, i) => ({ id: i, client: m.client, when: ago(m.at_s), length: m.length, text: m.text })),
)

const emptyCapture = computed(() => (lab.value?.capture ? 'Nothing recorded yet' : 'Capture is off'))

// ---- Capture controls ----
const busy = ref(false)

async function act(action: () => Promise<unknown>, failure: string): Promise<void> {
  busy.value = true
  try {
    await action()
    polling.stop()
    polling.start()
  } catch (caught) {
    show(`${failure}: ${describeError(caught)}`, { tone: 'error' })
  } finally {
    busy.value = false
  }
}

function toggleCapture(): void {
  if (lab.value) void act(() => setCapture(!lab.value!.capture), 'Could not change the capture')
}

async function clearAll(): Promise<void> {
  const ok = await confirm({
    title: 'Clear the capture?',
    message: 'Every recorded flow, DNS name and lab service request is forgotten, and the counters start from zero.',
    confirmLabel: 'Clear',
    tone: 'danger',
  })
  if (ok) await act(clearCapture, 'Could not clear the capture')
}

// What the device answers is the whole record (every table is bounded), so it is the export too.
function exportJson(): void {
  const blob = new Blob([JSON.stringify(lab.value, null, 2)], { type: 'application/json' })
  const link = document.createElement('a')
  link.href = URL.createObjectURL(blob)
  link.download = `netlab-capture-${new Date().toISOString().replace(/[:.]/g, '-')}.json`
  link.click()
  URL.revokeObjectURL(link.href)
}

// ---- Lab service ----
const labText = ref('')
const sending = ref(false)

async function sendToLab(): Promise<void> {
  sending.value = true
  try {
    const answer = await sendLab(labText.value)
    show(`The lab service received ${answer.length} bytes.`)
    labText.value = ''
    polling.stop()
    polling.start()
  } catch (caught) {
    show(`Could not send: ${describeError(caught)}`, { tone: 'error' })
  } finally {
    sending.value = false
  }
}
</script>

<template>
  <div class="nl-stack">
    <NlPageHeader title="Router lab" subtitle="The lab network, its clients, and what they reach while capture is on" />

    <LoadNotice
      :loading="state === null && !error"
      :error="error"
      what="the router lab"
      :stale="state !== null"
      @retry="polling.stop(); polling.start()"
    />

    <NlCard v-if="state && !state.enabled">
      <template #head>
        <NlCardHead title="Router lab mode is off" subtitle="This firmware is built without it" />
      </template>
      <p class="nl-hint">
        Enable it in <span class="nl-mono">menuconfig</span> → Wi-Fi bridge → Router lab mode, then build and flash again.
        Read the lab rules in the README first.
      </p>
    </NlCard>

    <template v-else-if="lab">
      <p v-if="lab.problem" class="nl-notice nl-notice-warn" role="status">
        <NlIcon name="triangle-alert" /><span>{{ lab.problem }}</span>
      </p>

      <section class="nl-grid-kpi" aria-label="Key figures">
        <NlKpiTile
          label="Lab network"
          :value="lab.active ? 'Routing' : 'Closed'"
          :sub="lab.active ? `${lab.ssid} · channel ${lab.channel}` : 'opens once the device is online'"
        />
        <NlKpiTile label="Clients" :value="`${lab.clients.length} of ${lab.max_clients}`" :sub="lab.ip" />
        <NlKpiTile
          label="Sent by lab clients"
          :value="formatBytes(lab.bytes)"
          :sub="`${lab.packets.toLocaleString('en-US')} packets`"
        />
        <NlKpiTile
          label="Flows"
          :value="`${lab.flows.length} of ${lab.flows_max}`"
          :sub="`${lab.flows_dropped} dropped (table full)`"
        />
      </section>

      <NlCard>
        <template #head>
          <NlCardHead title="Capture" subtitle="Which addresses and DNS names lab clients reach, never what they send">
            <template #actions>
              <NlPill :tone="lab.capture ? 'warning' : 'neutral'">{{ lab.capture ? 'Recording' : 'Off' }}</NlPill>
            </template>
          </NlCardHead>
        </template>
        <p class="nl-hint" style="margin-bottom: 12px">
          Start it only when the class has agreed. Records live in the device's RAM: a reboot or Clear forgets them.
          Capture is changed from the uplink network, not from the lab network.
        </p>
        <div class="nl-cluster">
          <button
            type="button"
            class="nl-btn"
            :class="lab.capture ? 'nl-btn-neutral' : 'nl-btn-primary'"
            :disabled="busy"
            @click="toggleCapture"
          >
            {{ lab.capture ? 'Stop capture' : 'Start capture' }}
          </button>
          <button type="button" class="nl-btn nl-btn-danger" :disabled="busy" @click="clearAll">Clear…</button>
          <button type="button" class="nl-btn nl-btn-neutral" @click="exportJson">Export JSON</button>
        </div>
      </NlCard>

      <NlCard flush>
        <template #head>
          <NlCardHead title="Clients" subtitle="Devices on the lab network" />
        </template>
        <NlDataTable
          :columns="clientColumns"
          :rows="clientRows"
          row-key="mac"
          caption="Lab network clients"
          empty-text="No device on the lab network"
        />
      </NlCard>

      <NlCard flush>
        <template #head>
          <NlCardHead title="Flows" subtitle="Counted from the client to the destination only; replies are not counted" />
        </template>
        <NlDataTable :columns="flowColumns" :rows="flowRows" row-key="id" caption="Flows" :empty-text="emptyCapture">
          <template #cell-dst="{ value }"><span class="nl-mono">{{ value }}</span></template>
        </NlDataTable>
      </NlCard>

      <NlCard flush>
        <template #head>
          <NlCardHead title="DNS names" subtitle="The most recent names lab clients asked for, newest first" />
        </template>
        <NlDataTable :columns="dnsColumns" :rows="dnsRows" row-key="id" caption="DNS names" :empty-text="emptyCapture" />
      </NlCard>

      <NlCard>
        <template #head>
          <NlCardHead title="Lab service" subtitle="The device's own plain-HTTP test server" />
        </template>
        <p class="nl-hint" style="margin-bottom: 12px">
          Send text and see what the server received. Without TLS, anyone on the path reads it the same way. Never type
          a real password here.
        </p>
        <form class="nl-chat-form" @submit.prevent="sendToLab">
          <input
            v-model="labText"
            class="nl-input"
            type="text"
            aria-label="Text for the lab service"
            placeholder="hello from the lab"
            autocomplete="off"
            :disabled="!lab.capture || sending"
          />
          <button type="submit" class="nl-btn nl-btn-primary" :disabled="!lab.capture || sending || !labText.trim()">
            <NlIcon name="send" /> Send
          </button>
        </form>
        <p v-if="!lab.capture" class="nl-hint" style="margin-top: 8px">Start the capture to use it.</p>
      </NlCard>

      <NlCard v-if="labRows.length" flush>
        <template #head>
          <NlCardHead title="Lab service requests" subtitle="Newest first" />
        </template>
        <NlDataTable :columns="labColumns" :rows="labRows" row-key="id" caption="Lab service requests">
          <template #cell-text="{ value }"><span class="nl-mono">{{ value }}</span></template>
        </NlDataTable>
      </NlCard>
    </template>
  </div>
</template>
