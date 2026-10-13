import { describe, expect, it } from 'vitest'
import type { MemoryRegion, MemorySnapshot } from './memory'
import {
  appendSample,
  formatMinutes,
  fragPercentSeries,
  fragmentation10,
  freeWindow,
  historyToCsv,
  STABLE_RANGE_BYTES,
  STABILITY_WINDOW_MS,
  minutesToLimit,
  pointFromSnapshot,
  readingsStalled,
  seedFromTrend,
  seriesStats,
  sliceRange,
  type HistoryPoint,
  type SeriesStats,
} from './memory-history'

function region(free: number, largest: number, total = 1000): MemoryRegion {
  return { total, free, min: free, largest, frag10: fragmentation10(free, largest), used_blocks: 10, free_blocks: 2 }
}

function snapshot(over: Partial<MemorySnapshot> = {}): MemorySnapshot {
  return {
    ht: 2,
    seq: 50,
    t_ms: 50_000,
    since_ms: 0,
    period_ms: 1000,
    dt_ms: 1000,
    self_us: 900,
    features: 5,
    cpu10: [123],
    regions: { internal: region(80, 40), dma: null, psram: null },
    failures: 0,
    alerts: [],
    trend: { len: 3, internal_free: [100, 90, 80], internal_largest: [50, 50, 40] },
    tasks: [],
    tasks_truncated: false,
    ...over,
  }
}

describe('fragmentation10', () => {
  it('is heaptop formula: the free memory outside the largest block, x10 percent', () => {
    expect(fragmentation10(100, 100)).toBe(0)
    expect(fragmentation10(100, 40)).toBe(600)
    expect(fragmentation10(200, 50)).toBe(750)
  })

  it('is 0 when nothing is free', () => {
    expect(fragmentation10(0, 0)).toBe(0)
  })
})

describe('pointFromSnapshot', () => {
  it('reads the regions and the load', () => {
    const point = pointFromSnapshot(snapshot())
    expect(point?.seq).toBe(50)
    expect(point?.t).toBe(50_000)
    expect(point?.internal.free).toBe(80)
    expect(point?.dma).toBeNull()
    expect(point?.cpu10).toEqual([123])
  })

  it('has no point without an internal region', () => {
    expect(pointFromSnapshot(snapshot({ regions: { internal: null, dma: null, psram: null } }))).toBeNull()
  })
})

describe('seedFromTrend', () => {
  it('spaces the earlier trend samples by the period and leaves the newest to the caller', () => {
    const seeded = seedFromTrend(snapshot())
    expect(seeded.map((point) => point.seq)).toEqual([48, 49])
    expect(seeded.map((point) => point.t)).toEqual([48_000, 49_000])
    expect(seeded.map((point) => point.internal.free)).toEqual([100, 90])
    expect(seeded.map((point) => point.internal.frag10)).toEqual([500, 444])
  })

  it('has nothing to seed from without a trend', () => {
    expect(seedFromTrend(snapshot({ trend: undefined }))).toEqual([])
  })

  it('drops samples from before boot', () => {
    const seeded = seedFromTrend(snapshot({ seq: 2, t_ms: 2000 }))
    expect(seeded.map((point) => point.seq)).toEqual([1])
  })
})

describe('appendSample', () => {
  it('starts from the trend and adds the real sample', () => {
    expect(appendSample([], snapshot()).map((point) => point.seq)).toEqual([48, 49, 50])
  })

  it('ignores a sample it has already seen', () => {
    const once = appendSample([], snapshot())
    expect(appendSample(once, snapshot())).toBe(once)
  })

  it('adds a newer sample', () => {
    const once = appendSample([], snapshot())
    const twice = appendSample(once, snapshot({ seq: 51, t_ms: 51_000 }))
    expect(twice.map((point) => point.seq)).toEqual([48, 49, 50, 51])
  })

  it('starts over when the device restarted (the clock went back)', () => {
    const once = appendSample([], snapshot())
    const restarted = appendSample(once, snapshot({ seq: 1, t_ms: 1000, trend: { len: 1, internal_free: [80], internal_largest: [40] } }))
    expect(restarted.map((point) => point.seq)).toEqual([1])
  })

  it('keeps at most `capacity` points', () => {
    expect(appendSample([], snapshot(), 2).map((point) => point.seq)).toEqual([49, 50])
  })
})

function at(t: number): HistoryPoint {
  return { seq: t, t, internal: { total: 1000, free: 500, largest: 250, frag10: 500 }, dma: null, psram: null, cpu10: [] }
}

describe('sliceRange', () => {
  const points = [at(0), at(30_000), at(60_000), at(90_000)]

  it('keeps the last stretch before the newest point', () => {
    expect(sliceRange(points, 60_000).map((point) => point.t)).toEqual([30_000, 60_000, 90_000])
  })

  it('keeps everything for no limit, and handles an empty history', () => {
    expect(sliceRange(points, null)).toBe(points)
    expect(sliceRange([], 60_000)).toEqual([])
  })
})

describe('seriesStats', () => {
  it('measures a falling line exactly', () => {
    const stats = seriesStats([0, 60_000, 120_000], [10, 8, 6])
    expect(stats).toMatchObject({ count: 3, first: 10, last: 6, min: 6, max: 10, mean: 8, delta: -4 })
    expect(stats?.slopePerMin).toBeCloseTo(-2)
    expect(stats?.r2).toBeCloseTo(1)
  })

  it('has no r2 for a flat line', () => {
    const stats = seriesStats([0, 60_000, 120_000], [5, 5, 5])
    expect(stats?.slopePerMin).toBe(0)
    expect(stats?.r2).toBeNull()
  })

  it('has no slope for one point', () => {
    const stats = seriesStats([0], [5])
    expect(stats?.slopePerMin).toBeNull()
    expect(stats?.mean).toBe(5)
  })

  it('skips gaps', () => {
    const stats = seriesStats([0, 60_000, 120_000], [1, null, 3])
    expect(stats?.count).toBe(2)
    expect(stats?.mean).toBe(2)
    expect(stats?.slopePerMin).toBeCloseTo(1)
  })

  it('has nothing for no readings', () => {
    expect(seriesStats([], [])).toBeNull()
    expect(seriesStats([0, 1], [null, null])).toBeNull()
  })
})

describe('minutesToLimit', () => {
  const stats = (over: Partial<SeriesStats>): SeriesStats => ({
    count: 30,
    first: 120,
    last: 100,
    min: 100,
    max: 120,
    mean: 110,
    delta: -20,
    slopePerMin: -2,
    r2: 0.9,
    ...over,
  })

  it('projects a floor that is being approached', () => {
    expect(minutesToLimit(stats({}), 40, 'floor')).toBeCloseTo(30)
  })

  it('projects a ceiling that is being approached', () => {
    expect(minutesToLimit(stats({ last: 60, slopePerMin: 2 }), 100, 'ceiling')).toBeCloseTo(20)
  })

  it('gives no answer without enough points, or for a noisy line', () => {
    expect(minutesToLimit(stats({ count: 29 }), 40, 'floor')).toBeNull()
    expect(minutesToLimit(stats({ r2: 0.4 }), 40, 'floor')).toBeNull()
    expect(minutesToLimit(null, 40, 'floor')).toBeNull()
  })

  it('gives no answer when the value moves away from the limit or is already past it', () => {
    expect(minutesToLimit(stats({ slopePerMin: 1 }), 40, 'floor')).toBeNull()
    expect(minutesToLimit(stats({ last: 30 }), 40, 'floor')).toBeNull()
    expect(minutesToLimit(stats({ slopePerMin: -1 }), 140, 'ceiling')).toBeNull()
  })
})

describe('formatMinutes', () => {
  it('words a duration', () => {
    expect(formatMinutes(0.5)).toBe('less than 1 min')
    expect(formatMinutes(12)).toBe('about 12 min')
    expect(formatMinutes(120)).toBe('about 2 h')
    expect(formatMinutes(2000)).toBe('more than 1 day')
  })
})

describe('historyToCsv', () => {
  it('writes one row per point with a column per core', () => {
    const first: HistoryPoint = { ...at(1000), seq: 1, cpu10: [123] }
    const second: HistoryPoint = { ...at(3000), seq: 2 }
    expect(historyToCsv([first, second])).toBe(
      [
        'seq,uptime_s,internal_free_b,internal_largest_b,internal_frag_pct,dma_frag_pct,cpu_core0_pct',
        '1,1,500,250,50,,12.3',
        '2,3,500,250,50,,',
        '',
      ].join('\n'),
    )
  })

  it('fills the DMA fragmentation where the point has a DMA region, and leaves it blank where it has none', () => {
    const seeded: HistoryPoint = { ...at(1000), seq: 1 }
    const polled: HistoryPoint = { ...at(3000), seq: 2, dma: { total: 900, free: 400, largest: 300, frag10: 38 } }
    expect(historyToCsv([seeded, polled])).toBe(
      [
        'seq,uptime_s,internal_free_b,internal_largest_b,internal_frag_pct,dma_frag_pct',
        '1,1,500,250,50,',
        '2,3,500,250,50,3.8',
        '',
      ].join('\n'),
    )
  })
})

describe('fragPercentSeries', () => {
  const internalOnly = at(1000)
  const withDma: HistoryPoint = { ...at(3000), dma: { total: 900, free: 400, largest: 300, frag10: 38 } }

  it('reads the DMA region and leaves a gap where a point has none (the seeded ones)', () => {
    expect(fragPercentSeries([internalOnly, withDma], true)).toEqual([null, 3.8])
  })

  it('reads the internal region when the firmware has no DMA region', () => {
    expect(fragPercentSeries([internalOnly, withDma], false)).toEqual([50, 50])
  })

  it('is empty for no points', () => {
    expect(fragPercentSeries([], true)).toEqual([])
  })
})

describe('freeWindow', () => {
  // One point every 2 s (the page's poll) with the free bytes given for each region.
  function series(internal: number[], dma: (number | null)[] = []): HistoryPoint[] {
    return internal.map((free, i) => {
      const dmaFree = dma[i]
      return {
        seq: i + 1,
        t: i * 2000,
        internal: { total: 400_000, free, largest: 160_000, frag10: 0 },
        dma: dmaFree == null ? null : { total: 390_000, free: dmaFree, largest: 160_000, frag10: 0 },
        psram: null,
        cpu10: [],
      }
    })
  }

  it('calls a flat line stable and reports its range and change', () => {
    const wobble = Array.from({ length: 41 }, (_, i) => 180_000 + (i % 3) * 64) // ends on i = 40, a 64 B step
    const window = freeWindow(series(wobble), 'internal')
    expect(window?.stability).toBe('stable')
    expect(window?.min).toBe(180_000)
    expect(window?.max).toBe(180_128)
    expect(window?.first).toBe(180_000)
    expect(window?.last).toBe(180_064)
    expect(window?.delta).toBe(64)
    expect(window?.count).toBe(41)
  })

  it('calls a steady fall falling and a steady climb rising', () => {
    const down = Array.from({ length: 60 }, (_, i) => 180_000 - i * 400)
    expect(freeWindow(series(down), 'internal')?.stability).toBe('falling')
    const up = Array.from({ length: 60 }, (_, i) => 100_000 + i * 400)
    expect(freeWindow(series(up), 'internal')?.stability).toBe('rising')
  })

  it('calls a big range without a trend varying', () => {
    const zigzag = Array.from({ length: 60 }, (_, i) => (i % 2 === 0 ? 150_000 : 170_000))
    expect(freeWindow(series(zigzag), 'internal')?.stability).toBe('varying')
  })

  it('looks only at the last window', () => {
    const old = Array.from({ length: 200 }, (_, i) => 100_000 + i * 1000) // 400 s, and far from what follows
    const recent = Array.from({ length: 160 }, () => 180_000) // 320 s at one point per 2 s: longer than the window
    const window = freeWindow(series([...old, ...recent]), 'internal', STABILITY_WINDOW_MS)
    expect(window?.min).toBe(180_000)
    expect(window?.count).toBe(151) // 300 s back from the newest point, both ends included
    expect(window?.stability).toBe('stable')
  })

  it('reads the DMA region and skips the points that have none', () => {
    const gaps = [null, null, 166_000, 166_100, 166_000, 166_050]
    const window = freeWindow(series([180_000, 180_000, 180_000, 180_100, 180_000, 180_050], gaps), 'dma')
    expect(window?.count).toBe(4)
    expect(window?.min).toBe(166_000)
    expect(window?.max).toBe(166_100)
  })

  it('has nothing for a region without readings, and says so for too few', () => {
    expect(freeWindow(series([180_000, 180_000]), 'dma')).toBeNull()
    expect(freeWindow([], 'internal')).toBeNull()
    expect(freeWindow(series([180_000, 180_000]), 'internal')?.stability).toBe('unknown')
  })

  it('treats a range just under the stable limit as stable and just over it as not', () => {
    const under = [180_000, 180_000 + STABLE_RANGE_BYTES - 1, 180_000]
    expect(freeWindow(series(under), 'internal')?.stability).toBe('stable')
    const over = [180_000, 180_000 + STABLE_RANGE_BYTES, 180_000]
    expect(freeWindow(series(over), 'internal')?.stability).not.toBe('stable')
  })
})

describe('readingsStalled', () => {
  it('waits three polls before calling the readings stopped', () => {
    expect(readingsStalled(6000, 1000)).toBe(false)
    expect(readingsStalled(6001, 1000)).toBe(true)
  })

  it('waits three sample periods when heaptop samples slower than the page polls', () => {
    expect(readingsStalled(15_000, 5000)).toBe(false)
    expect(readingsStalled(15_001, 5000)).toBe(true)
  })

  it('is not stopped when the newest sample came after the last answer was timed', () => {
    expect(readingsStalled(-5, 1000)).toBe(false)
  })
})
