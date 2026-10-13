import { describe, expect, it } from 'vitest'
import {
  NVS_ERROR_PCT,
  NVS_WARN_PCT,
  describePartition,
  entriesBytes,
  flashLayout,
  formatAddress,
  nvsCapacity,
  nvsTone,
  nvsUsagePct,
  partitionRole,
  sortNamespaces,
  type StorageNvs,
  type StoragePartition,
  type StorageSnapshot,
} from './storage'

function partition(label: string, type: string, subtype: string, address: number, size: number, running = false): StoragePartition {
  return { label, type, subtype, address, size, encrypted: false, running }
}

// partitions.csv of netlab: a 16 MB chip, a 24 KiB NVS, one factory app of 2 MB and a 1 MB web partition.
const nvsPartition = partition('nvs', 'data', 'nvs', 0x9000, 0x6000)
const phyInit = partition('phy_init', 'data', 'phy', 0xf000, 0x1000)
const factory = partition('factory', 'app', 'factory', 0x10000, 0x200000, true)
const www = partition('www', 'data', 'littlefs', 0x210000, 0x100000)

const nvs: StorageNvs = {
  label: 'nvs',
  entry_bytes: 32,
  total: 756,
  used: 96,
  available: 630,
  namespaces: [
    { name: 'phy', entries: 10 },
    { name: 'nvs.net80211', entries: 80 },
    { name: 'netlab', entries: 6 },
  ],
  namespaces_truncated: false,
}

function snapshot(over: Partial<StorageSnapshot> = {}): StorageSnapshot {
  return {
    flash_size: 0x1000000,
    partitions: [nvsPartition, phyInit, factory, www],
    partitions_truncated: false,
    nvs,
    ...over,
  }
}

describe('NVS usage', () => {
  it('is the share of the writable entries that are in use', () => {
    expect(nvsCapacity(nvs)).toBe(726)
    expect(nvsUsagePct(nvs)).toBeCloseTo(13.22, 2)
  })

  it('is 0 for a partition with no capacity, and never above 100', () => {
    expect(nvsUsagePct({ ...nvs, used: 0, available: 0 })).toBe(0)
    expect(nvsUsagePct({ ...nvs, used: 10, available: 0 })).toBe(100)
  })

  it('turns to a warning and then an error at the page limits', () => {
    expect(nvsTone(NVS_WARN_PCT - 0.1)).toBe('ok')
    expect(nvsTone(NVS_WARN_PCT)).toBe('warn')
    expect(nvsTone(NVS_ERROR_PCT - 0.1)).toBe('warn')
    expect(nvsTone(NVS_ERROR_PCT)).toBe('error')
  })

  it('turns entries into bytes with the entry size heaptop reports', () => {
    expect(entriesBytes(80, nvs)).toBe(2560)
    expect(entriesBytes(80, { ...nvs, entry_bytes: 64 })).toBe(5120)
  })
})

describe('sortNamespaces', () => {
  it('puts the biggest first, ties by name, and leaves the list alone', () => {
    const list = [...nvs.namespaces, { name: 'aaa', entries: 10 }]
    expect(sortNamespaces(list).map((ns) => ns.name)).toEqual(['nvs.net80211', 'aaa', 'phy', 'netlab'])
    expect(list.map((ns) => ns.name)).toEqual(['phy', 'nvs.net80211', 'netlab', 'aaa'])
  })
})

describe('partitionRole', () => {
  it('tells firmware, NVS, files and the rest apart', () => {
    expect(partitionRole(factory)).toBe('app')
    expect(partitionRole(partition('ota_1', 'app', 'ota_1', 0, 1))).toBe('app')
    expect(partitionRole(nvsPartition)).toBe('nvs')
    expect(partitionRole(www)).toBe('files')
    expect(partitionRole(partition('s', 'data', 'spiffs', 0, 1))).toBe('files')
    expect(partitionRole(phyInit)).toBe('system')
    expect(partitionRole(partition('boot', 'bootloader', 'primary', 0, 1))).toBe('system')
    expect(partitionRole(partition('x', '0x7f', '0x05', 0, 1))).toBe('system')
  })
})

describe('describePartition', () => {
  it('words the known ones', () => {
    expect(describePartition(factory)).toBe('Firmware (factory)')
    expect(describePartition(partition('o', 'app', 'ota_15', 0, 1))).toBe('Firmware (OTA 15)')
    expect(describePartition(nvsPartition)).toBe('NVS storage')
    expect(describePartition(partition('d', 'data', 'ota', 0, 1))).toBe('OTA slot selection')
    expect(describePartition(phyInit)).toBe('Radio calibration')
    expect(describePartition(www)).toBe('Files (LittleFS)')
    expect(describePartition(partition('b', 'bootloader', 'primary', 0, 1))).toBe('Bootloader')
    expect(describePartition(partition('t', 'partition_table', 'primary', 0, 1))).toBe('Partition table')
  })

  it('shows the raw type and subtype for one it does not know', () => {
    expect(describePartition(partition('m', 'data', '0x99', 0, 1))).toBe('data / 0x99')
    expect(describePartition(partition('m', '0x7f', '0x05', 0, 1))).toBe('0x7f / 0x05')
  })
})

describe('flashLayout', () => {
  it('lists the partitions by address, then what is outside them, and adds up to the chip', () => {
    const segments = flashLayout(snapshot({ partitions: [www, factory, nvsPartition, phyInit] }))
    expect(segments.map((segment) => segment.label)).toEqual(['nvs', 'phy_init', 'factory', 'www', 'Outside the partitions'])
    expect(segments.reduce((sum, segment) => sum + segment.bytes, 0)).toBe(0x1000000)
    expect(segments.reduce((sum, segment) => sum + segment.pct, 0)).toBeCloseTo(100, 6)
    const outside = segments[segments.length - 1]
    expect(outside?.role).toBe('outside')
    expect(outside?.bytes).toBe(0x1000000 - 0x307000)
  })

  it('marks the running app', () => {
    const running = flashLayout(snapshot()).filter((segment) => segment.running)
    expect(running.map((segment) => segment.label)).toEqual(['factory'])
  })

  it('takes the share of each segment from the chip size', () => {
    const app = flashLayout(snapshot()).find((segment) => segment.label === 'factory')
    expect(app?.pct).toBeCloseTo((0x200000 / 0x1000000) * 100, 6)
    expect(app?.role).toBe('app')
  })

  it('has no outside part when the partitions fill the chip, or overlap past it', () => {
    const full = flashLayout(snapshot({ flash_size: 0x200000, partitions: [factory] }))
    expect(full.map((segment) => segment.label)).toEqual(['factory'])
    const over = flashLayout(snapshot({ flash_size: 0x100000, partitions: [factory] }))
    expect(over.map((segment) => segment.label)).toEqual(['factory'])
  })

  it('measures against the partitions themselves when the chip size is unknown', () => {
    const segments = flashLayout(snapshot({ flash_size: null, partitions: [factory, www] }))
    expect(segments.map((segment) => segment.label)).toEqual(['factory', 'www'])
    expect(segments[0]?.pct).toBeCloseTo((2 / 3) * 100, 6)
    expect(segments[1]?.pct).toBeCloseTo((1 / 3) * 100, 6)
  })

  it('is only the outside part without partitions, and empty without a chip size either', () => {
    expect(flashLayout(snapshot({ partitions: [] }))).toEqual([
      { key: 'outside', label: 'Outside the partitions', role: 'outside', bytes: 0x1000000, pct: 100, running: false },
    ])
    expect(flashLayout(snapshot({ partitions: [], flash_size: null }))).toEqual([])
  })
})

describe('formatAddress', () => {
  it('is upper-case hex', () => {
    expect(formatAddress(0x9000)).toBe('0x9000')
    expect(formatAddress(0x210000)).toBe('0x210000')
    expect(formatAddress(0)).toBe('0x0')
  })
})
