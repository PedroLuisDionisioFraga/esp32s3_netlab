import { ref, shallowRef, watch, type Ref } from 'vue'
import type { MemorySnapshot } from './memory'
import { appendSample, type HistoryPoint } from './memory-history'

/**
 * Remembers the snapshots the page polls, newest last, for the charts in the tile modals. The array is replaced on
 * every new sample (never mutated), so it can sit in a shallow ref. `endWallMs` is when the newest sample arrived.
 */
export function useMemoryHistory(snapshot: Ref<MemorySnapshot | null>) {
  const points = shallowRef<readonly HistoryPoint[]>([])
  const endWallMs = ref<number | null>(null)

  watch(
    snapshot,
    (current) => {
      if (!current) return
      const next = appendSample(points.value, current)
      // The same array back means this sample was already seen: the arrival time stays that of the first sight.
      if (next !== points.value) {
        points.value = next
        endWallMs.value = Date.now()
      }
    },
    { immediate: true },
  )

  /** Forget everything: heaptop's windows start over on "Clear statistics", and the charts follow. */
  function reset(): void {
    points.value = []
    endWallMs.value = null
    if (snapshot.value) {
      points.value = appendSample([], snapshot.value)
      endWallMs.value = Date.now()
    }
  }

  return { points, endWallMs, reset }
}
