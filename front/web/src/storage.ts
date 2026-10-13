// Flash and NVS: what the device sends (GET /api/v1/storage, heaptop 0.5.0) and what the page makes of it.
// heaptop reports raw facts only: bytes, entries and ESP-IDF names. The limits, the wording and the grouping below
// are this page's own. Pure (no Vue, no fetch), so Vitest can load it; the request is in storage-api.ts.

export interface StoragePartition {
  label: string
  /** ESP-IDF name ("app", "data", ...), or hex ("0x7f") for an id heaptop does not know. */
  type: string
  /** ESP-IDF name ("ota_0", "nvs", "littlefs", ...), or hex. Its meaning depends on the type. */
  subtype: string
  address: number
  size: number
  encrypted: boolean
  /** The app partition this firmware runs from. */
  running: boolean
}

export interface StorageNamespace {
  name: string
  /** Entries the namespace uses, its own entry included. A blob uses several. */
  entries: number
}

export interface StorageNvs {
  label: string
  /** Bytes in one entry: entries x this is the space they take. */
  entry_bytes: number
  total: number
  used: number
  /** Entries that can still be written: what is not used, less the page NVS keeps in reserve. */
  available: number
  namespaces: StorageNamespace[]
  namespaces_truncated: boolean
}

export interface StorageSnapshot {
  /** Chip size in bytes; null when the firmware could not read it. */
  flash_size: number | null
  partitions: StoragePartition[]
  partitions_truncated: boolean
  /** null when the NVS statistics could not be read. */
  nvs: StorageNvs | null
}

/** NVS use, in percent of the writable entries, from which the bar and the pill warn and then turn to an error. */
export const NVS_WARN_PCT = 80
export const NVS_ERROR_PCT = 90

export type StorageTone = 'ok' | 'warn' | 'error'

/** What NVS can ever hold: the entries in use plus those still writable. The reserved page is not counted. */
export function nvsCapacity(nvs: StorageNvs): number {
  return nvs.used + nvs.available
}

/** Percent of the capacity in use, 0 to 100. */
export function nvsUsagePct(nvs: StorageNvs): number {
  const capacity = nvsCapacity(nvs)
  return capacity > 0 ? Math.min(100, (nvs.used / capacity) * 100) : 0
}

export function nvsTone(pct: number): StorageTone {
  return pct >= NVS_ERROR_PCT ? 'error' : pct >= NVS_WARN_PCT ? 'warn' : 'ok'
}

export function entriesBytes(entries: number, nvs: StorageNvs): number {
  return entries * nvs.entry_bytes
}

/** The biggest namespace first, ties by name. A new array: the document is not touched. */
export function sortNamespaces(namespaces: readonly StorageNamespace[]): StorageNamespace[] {
  return [...namespaces].sort((a, b) => b.entries - a.entries || a.name.localeCompare(b.name))
}

/** The colour group of a partition in the flash bar. `outside` is what no partition covers. */
export type PartitionRole = 'app' | 'nvs' | 'files' | 'system' | 'outside'

const FILE_SUBTYPES: readonly string[] = ['littlefs', 'spiffs', 'fat', 'esphttpd']

export function partitionRole(partition: StoragePartition): Exclude<PartitionRole, 'outside'> {
  if (partition.type === 'app') return 'app'
  if (partition.type === 'data' && partition.subtype === 'nvs') return 'nvs'
  if (partition.type === 'data' && FILE_SUBTYPES.includes(partition.subtype)) return 'files'
  return 'system'
}

const DATA_SUBTYPE_LABELS: Readonly<Record<string, string>> = {
  ota: 'OTA slot selection',
  phy: 'Radio calibration',
  nvs: 'NVS storage',
  nvs_keys: 'NVS keys',
  coredump: 'Crash dump (coredump)',
  efuse_em: 'eFuse emulation',
  littlefs: 'Files (LittleFS)',
  spiffs: 'Files (SPIFFS)',
  fat: 'Files (FAT)',
}

/** What a partition is for, in words; the raw type and subtype for one this page does not know. */
export function describePartition(partition: StoragePartition): string {
  const { type, subtype } = partition
  if (type === 'app') {
    if (subtype === 'factory') return 'Firmware (factory)'
    const slot = /^ota_(\d+)$/.exec(subtype)
    if (slot) return `Firmware (OTA ${slot[1]})`
    if (subtype === 'test') return 'Test firmware'
  }
  if (type === 'data' && DATA_SUBTYPE_LABELS[subtype]) return DATA_SUBTYPE_LABELS[subtype]
  if (type === 'bootloader') return 'Bootloader'
  if (type === 'partition_table') return 'Partition table'
  return `${type} / ${subtype}`
}

export interface LayoutSegment {
  key: string
  label: string
  role: PartitionRole
  bytes: number
  /** Share of the bar, in percent. */
  pct: number
  running: boolean
}

/**
 * The flash as consecutive segments: each partition by address, then what no partition covers (the bootloader, the
 * partition table, gaps and unallocated space: the chip size less the partitions). With an unknown chip size the bar is
 * only the partitions.
 */
export function flashLayout(storage: StorageSnapshot): LayoutSegment[] {
  const partitions = [...storage.partitions].sort((a, b) => a.address - b.address)
  const covered = partitions.reduce((sum, partition) => sum + partition.size, 0)
  const flash = storage.flash_size ?? 0
  const whole = flash > 0 ? Math.max(flash, covered) : covered
  if (whole <= 0) return []

  const segments: LayoutSegment[] = partitions.map((partition) => ({
    key: `${partition.address}-${partition.label}`,
    label: partition.label,
    role: partitionRole(partition),
    bytes: partition.size,
    pct: (partition.size / whole) * 100,
    running: partition.running,
  }))
  if (flash > covered) {
    segments.push({
      key: 'outside',
      label: 'Outside the partitions',
      role: 'outside',
      bytes: flash - covered,
      pct: ((flash - covered) / whole) * 100,
      running: false,
    })
  }
  return segments
}

/** An offset in the flash as upper-case hex: 0x9000. */
export function formatAddress(address: number): string {
  return `0x${address.toString(16).toUpperCase()}`
}
