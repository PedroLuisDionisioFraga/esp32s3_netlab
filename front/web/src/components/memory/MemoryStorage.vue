<script setup lang="ts">
import { computed } from 'vue'
import { formatBytes } from '../../format'
import {
  describePartition,
  entriesBytes,
  flashLayout,
  formatAddress,
  nvsTone,
  nvsUsagePct,
  sortNamespaces,
  type PartitionRole,
  type StorageSnapshot,
  type StorageTone,
} from '../../storage'
import NlCard from '../../ui/NlCard.vue'
import NlCardHead from '../../ui/NlCardHead.vue'
import NlDataTable from '../../ui/NlDataTable.vue'
import NlPill from '../../ui/NlPill.vue'
import type { PillTone, TableColumn } from '../../ui/types'

// Flash and NVS as heaptop reports them (raw bytes and entries): what the chip holds and how full its NVS is. The
// wording, the colours and the 80 % / 90 % limits are this page's (storage.ts).
const props = defineProps<{ storage: StorageSnapshot }>()

const ROLE_COLORS: Readonly<Record<PartitionRole, string>> = {
  app: 'var(--nl-series-1)',
  nvs: 'var(--nl-series-3)',
  files: 'var(--nl-series-2)',
  system: 'var(--nl-muted)',
  outside: 'var(--nl-map-scattered)',
}

const PILL_TONES: Readonly<Record<StorageTone, PillTone>> = { ok: 'success', warn: 'warning', error: 'error' }

const number = (value: number) => value.toLocaleString('en-US')
const percent = (value: number) => `${value.toLocaleString('en-US', { maximumFractionDigits: 1 })} %`

const subtitle = computed(() => {
  const { flash_size: flash, partitions } = props.storage
  const count = `${partitions.length} ${partitions.length === 1 ? 'partition' : 'partitions'}`
  return flash ? `${formatBytes(flash)} of flash · ${count}` : `Flash size unknown · ${count}`
})

const segments = computed(() => flashLayout(props.storage))
const summary = computed(() =>
  segments.value.map((segment) => `${segment.label}: ${formatBytes(segment.bytes)} (${percent(segment.pct)})`).join('; '),
)

const nvs = computed(() => props.storage.nvs)
const usagePct = computed(() => (nvs.value ? nvsUsagePct(nvs.value) : 0))
const tone = computed(() => nvsTone(usagePct.value))

const namespaceColumns: TableColumn[] = [
  { key: 'name', label: 'Namespace', text: true },
  { key: 'entries', label: 'Entries' },
  { key: 'size', label: 'Space' },
  { key: 'share', label: 'Of NVS in use' },
]

const namespaceRows = computed(() => {
  const current = nvs.value
  if (!current) return []
  return sortNamespaces(current.namespaces).map((ns) => ({
    id: ns.name,
    name: ns.name,
    entries: number(ns.entries),
    size: formatBytes(entriesBytes(ns.entries, current)),
    share: current.used > 0 ? percent((ns.entries / current.used) * 100) : '—',
  }))
})

const partitionColumns: TableColumn[] = [
  { key: 'label', label: 'Partition', text: true },
  { key: 'role', label: 'Purpose', text: true },
  { key: 'address', label: 'Offset' },
  { key: 'size', label: 'Size' },
  { key: 'state', label: 'State' },
]

const partitionRows = computed(() =>
  [...props.storage.partitions]
    .sort((a, b) => a.address - b.address)
    .map((partition) => ({
      id: `${partition.address}-${partition.label}`,
      label: partition.label,
      role: describePartition(partition),
      address: formatAddress(partition.address),
      size: formatBytes(partition.size),
      state: [partition.running ? 'Running' : '', partition.encrypted ? 'Encrypted' : ''].filter(Boolean).join(' · ') || '—',
    })),
)
</script>

<template>
  <NlCard>
    <template #head>
      <NlCardHead title="Storage" :subtitle="subtitle" />
    </template>

    <div class="nl-region">
      <div class="nl-region-head">
        <h3>Flash</h3>
        <span v-if="storage.flash_size" class="nl-num">{{ formatBytes(storage.flash_size) }}</span>
      </div>
      <div v-if="segments.length > 0">
        <div class="nl-map-bar" role="img" :aria-label="`Flash map. ${summary}`">
          <span
            v-for="segment in segments"
            :key="segment.key"
            class="nl-map-seg"
            :style="{ flexGrow: segment.bytes, backgroundColor: ROLE_COLORS[segment.role] }"
            :title="`${segment.label}: ${formatBytes(segment.bytes)} (${percent(segment.pct)})`"
          />
        </div>
        <ul class="nl-map-legend">
          <li v-for="segment in segments" :key="segment.key">
            <span class="nl-key" :style="{ backgroundColor: ROLE_COLORS[segment.role] }" aria-hidden="true" />
            <span class="nl-map-label">
              {{ segment.label }}
              <NlPill v-if="segment.running" tone="info">Running</NlPill>
            </span>
            <strong class="nl-num">{{ formatBytes(segment.bytes) }}</strong>
            <span class="nl-map-pct nl-num">{{ percent(segment.pct) }}</span>
          </li>
        </ul>
      </div>
      <p v-else class="nl-limit-text">The device did not report its partitions.</p>
    </div>

    <div class="nl-region">
      <div class="nl-region-head">
        <h3>NVS</h3>
        <NlPill v-if="nvs" :tone="PILL_TONES[tone]">{{ percent(usagePct) }} in use</NlPill>
      </div>
      <template v-if="nvs">
        <div
          class="nl-bar"
          :class="{ 'is-alert': tone === 'error', 'is-warn': tone === 'warn' }"
          role="progressbar"
          aria-label="NVS in use"
          aria-valuemin="0"
          aria-valuemax="100"
          :aria-valuenow="Math.round(usagePct)"
        >
          <span :style="{ width: `${usagePct}%` }" />
        </div>
        <dl class="nl-dl">
          <dt>Entries in use</dt>
          <dd class="nl-num">{{ number(nvs.used) }} · {{ formatBytes(entriesBytes(nvs.used, nvs)) }}</dd>
          <dt>Still available</dt>
          <dd class="nl-num">{{ number(nvs.available) }} · {{ formatBytes(entriesBytes(nvs.available, nvs)) }}</dd>
          <dt>Partition total</dt>
          <dd class="nl-num">{{ number(nvs.total) }} · {{ formatBytes(entriesBytes(nvs.total, nvs)) }}</dd>
        </dl>
        <p class="nl-limit-text">
          Each entry is {{ nvs.entry_bytes }} B. NVS keeps one page free in reserve, which does not count as available.
        </p>
        <NlDataTable
          :columns="namespaceColumns"
          :rows="namespaceRows"
          row-key="id"
          caption="NVS use by namespace"
          empty-text="No namespace in NVS."
        />
        <p v-if="nvs.namespaces_truncated" class="nl-limit-text">There are more namespaces than the list shows.</p>
      </template>
      <p v-else class="nl-limit-text">The NVS statistics could not be read (not initialised yet?).</p>
    </div>

    <div class="nl-region">
      <div class="nl-region-head">
        <h3>Partition table</h3>
      </div>
      <NlDataTable
        :columns="partitionColumns"
        :rows="partitionRows"
        row-key="id"
        caption="Flash partitions"
        empty-text="No partition reported."
      />
      <p v-if="storage.partitions_truncated" class="nl-limit-text">There are more partitions than the table shows.</p>
    </div>
  </NlCard>
</template>
