import { describe, expect, it } from 'vitest'
import {
  areaPath,
  axisFor,
  domainFor,
  formatAgo,
  linePath,
  nearestIndex,
  niceTicks,
  scaleLinear,
  splitSegments,
  timeTicks,
} from './chart'

describe('scaleLinear', () => {
  it('maps the ends and the middle of the domain', () => {
    const scale = scaleLinear(0, 10, 100, 200)
    expect(scale(0)).toBe(100)
    expect(scale(10)).toBe(200)
    expect(scale(5)).toBe(150)
  })

  it('inverts for a screen y axis', () => {
    const scale = scaleLinear(0, 100, 300, 0)
    expect(scale(0)).toBe(300)
    expect(scale(100)).toBe(0)
  })

  it('puts a flat domain in the middle of the range', () => {
    expect(scaleLinear(7, 7, 0, 100)(7)).toBe(50)
  })
})

describe('niceTicks', () => {
  it('rounds 0-100 to steps of 25', () => {
    const axis = niceTicks(0, 100, 4)
    expect(axis.ticks).toEqual([0, 25, 50, 75, 100])
    expect(axis.min).toBe(0)
    expect(axis.max).toBe(100)
  })

  it('rounds in the display unit: a KiB axis reads 150 / 200 / 250 / 300', () => {
    const axis = niceTicks(150 * 1024 + 1024, 256 * 1024, 4, 1024)
    expect(axis.ticks).toEqual([150, 200, 250, 300].map((kib) => kib * 1024))
    expect(axis.min).toBe(150 * 1024)
    expect(axis.max).toBe(300 * 1024)
  })

  it('opens a window around a flat series', () => {
    const axis = niceTicks(5, 5)
    expect(axis.min).toBeLessThan(5)
    expect(axis.max).toBeGreaterThan(5)
    expect(axis.ticks).toContain(5)
  })

  it('falls back to 0-1 for values that are not numbers', () => {
    const axis = niceTicks(Number.NaN, 1)
    expect(axis.min).toBe(0)
    expect(axis.max).toBe(1)
  })

  it('does not leave float noise in the ticks', () => {
    for (const tick of niceTicks(0, 1, 5).ticks) expect(String(tick).length).toBeLessThan(8)
  })
})

describe('timeTicks and formatAgo', () => {
  it('picks the smallest step that keeps the ticks within the count', () => {
    expect(timeTicks(60_000, 5)).toEqual([0, 15_000, 30_000, 45_000, 60_000])
  })

  it('has one tick for an empty span', () => {
    expect(timeTicks(0)).toEqual([0])
  })

  it('words an offset', () => {
    expect(formatAgo(0)).toBe('now')
    expect(formatAgo(30_000)).toBe('-30 s')
    expect(formatAgo(120_000)).toBe('-2 min')
    expect(formatAgo(90_000)).toBe('-1 min 30 s')
  })
})

describe('nearestIndex', () => {
  const xs = [10, 20, 30, 40]

  it('finds the closest entry', () => {
    expect(nearestIndex(xs, 24)).toBe(1)
    expect(nearestIndex(xs, 26)).toBe(2)
  })

  it('clamps outside the data', () => {
    expect(nearestIndex(xs, -5)).toBe(0)
    expect(nearestIndex(xs, 100)).toBe(3)
  })

  it('prefers the earlier entry on a tie', () => {
    expect(nearestIndex(xs, 25)).toBe(1)
  })

  it('has no answer for an empty list', () => {
    expect(nearestIndex([], 1)).toBe(-1)
  })
})

describe('splitSegments', () => {
  it('breaks the line where two samples are further apart than the allowed gap', () => {
    expect(splitSegments([0, 1000, 2000, 10_000, 11_000], [1, 2, 3, 4, 5], 3000)).toEqual([
      [0, 2],
      [3, 4],
    ])
  })

  it('breaks it at a missing value', () => {
    expect(splitSegments([0, 1, 2], [1, null, 3], 1000)).toEqual([
      [0, 0],
      [2, 2],
    ])
  })

  it('skips leading and trailing gaps, and has no run for no data', () => {
    expect(splitSegments([0, 1, 2], [null, 2, 3], 1000)).toEqual([[1, 2]])
    expect(splitSegments([0, 1], [null, null], 1000)).toEqual([])
  })
})

describe('paths', () => {
  it('draws a line through the points', () => {
    expect(linePath([0, 10], [5, 15], 0, 1)).toBe('M0 5L10 15')
  })

  it('draws a lone point as a dot', () => {
    expect(linePath([0], [5], 0, 0)).toBe('M0 5l0.01 0')
    expect(linePath([0], [5], 1, 0)).toBe('')
  })

  it('closes an area down to the baseline', () => {
    expect(areaPath([0, 10], [5, 15], 0, 1, 20)).toBe('M0 5L10 15L10 20L0 20Z')
    expect(areaPath([0], [5], 0, 0, 20)).toBe('')
  })
})

describe('domains', () => {
  it('keeps a fixed range whatever the data', () => {
    expect(domainFor([1, 2, 3], { fixed: [0, 100] })).toEqual([0, 100])
  })

  it('pads the data and keeps the included values in view', () => {
    const [lo, hi] = domainFor([10, 20, null], { include: [0] })
    expect(lo).toBeCloseTo(-1.6)
    expect(hi).toBeCloseTo(21.6)
  })

  it('has a default range for no data', () => {
    expect(domainFor([null, null])).toEqual([0, 1])
  })

  it('gives a percentage axis its quarter ticks', () => {
    expect(axisFor([], { fixed: [0, 100] }).ticks).toEqual([0, 25, 50, 75, 100])
  })
})
