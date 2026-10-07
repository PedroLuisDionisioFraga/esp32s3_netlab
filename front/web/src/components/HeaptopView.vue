<script setup>
import { onMounted, onUnmounted, ref, watch } from 'vue'
import { clearHeaptop, getHeaptop } from '../api'
import { usePolling } from '../usePolling'

// Same range and steps as `ht top`: + doubles the refresh, - halves it.
const REFRESH_MIN_MS = 100
const REFRESH_MAX_MS = 10000

const VIEWS = [
  { key: 'top', label: 'Top' },
  { key: 'heap', label: 'Heap' },
  { key: 'tasks', label: 'Tasks' },
  { key: 'health', label: 'Health' },
]
// The c/m/s/n keys of `ht top`.
const SORTS = [
  { key: 'cpu', label: 'CPU', hotkey: 'c' },
  { key: 'heap', label: 'Memory', hotkey: 'm' },
  { key: 'stack', label: 'Stack', hotkey: 's' },
  { key: 'name', label: 'Name', hotkey: 'n' },
]

const view = ref('top')
const sort = ref('cpu')
const refreshMs = ref(1000)
const paused = ref(false)
const clearing = ref(false)
const notice = ref(null)

// The device renders the text (heaptop's own renderers), so it matches the serial console.
const { data: frame, error, refresh } = usePolling(
  () => getHeaptop({ view: view.value, sort: sort.value, refresh: refreshMs.value, paused: paused.value }),
  refreshMs,
  { enabled: () => !paused.value },
)

// Like a key press in `ht top`, every change redraws at once. While paused that draws one more
// frame, marked PAUSED in the top header, and the page stays frozen on it.
watch([view, sort, refreshMs, paused], () => refresh())

const sortable = () => view.value === 'top' || view.value === 'tasks'

function slower() {
  refreshMs.value = Math.min(refreshMs.value * 2, REFRESH_MAX_MS)
}

function faster() {
  refreshMs.value = Math.max(Math.floor(refreshMs.value / 2), REFRESH_MIN_MS)
}

function togglePause() {
  paused.value = !paused.value
}

async function clearStats() {
  clearing.value = true
  notice.value = null
  try {
    notice.value = (await clearHeaptop()).message
    refresh()
  } catch (err) {
    notice.value = err.message
  } finally {
    clearing.value = false
  }
}

function onKey(event) {
  if (event.ctrlKey || event.metaKey || event.altKey || event.target.closest?.('input, textarea, select')) {
    return
  }
  const byKey = SORTS.find((s) => s.hotkey === event.key)
  if (byKey && sortable()) {
    sort.value = byKey.key
  } else if (event.key === 'p') {
    togglePause()
  } else if (event.key === '+') {
    slower()
  } else if (event.key === '-') {
    faster()
  }
}

onMounted(() => window.addEventListener('keydown', onKey))
onUnmounted(() => window.removeEventListener('keydown', onKey))
</script>

<template>
  <section class="card wide">
    <h2>Heaptop</h2>

    <div class="row" role="tablist">
      <button
        v-for="v in VIEWS"
        :key="v.key"
        role="tab"
        :aria-selected="view === v.key"
        :class="{ active: view === v.key }"
        @click="view = v.key"
      >
        {{ v.label }}
      </button>
    </div>

    <div class="row action">
      <template v-if="sortable()">
        <span class="hint">Sort</span>
        <button
          v-for="s in SORTS"
          :key="s.key"
          :title="`Key ${s.hotkey}`"
          :class="{ active: sort === s.key }"
          @click="sort = s.key"
        >
          {{ s.label }}
        </button>
      </template>
      <button title="Key p" :class="{ active: paused }" @click="togglePause">{{ paused ? 'Resume' : 'Pause' }}</button>
      <span class="row">
        <button title="Key -" :disabled="refreshMs <= REFRESH_MIN_MS" @click="faster">−</button>
        <span class="hint">every {{ refreshMs }} ms</span>
        <button title="Key +" :disabled="refreshMs >= REFRESH_MAX_MS" @click="slower">+</button>
      </span>
      <button :disabled="clearing" title="Like ht clear" @click="clearStats">Clear stats</button>
    </div>

    <p v-if="notice" class="hint">{{ notice }}</p>
    <p v-if="error" class="error">{{ error }}</p>
    <pre v-if="frame !== null" class="terminal" :class="{ stale: error }">{{ frame }}</pre>
    <p v-else-if="!error" class="hint">Loading…</p>

    <p class="hint">
      The same text as the <code>ht</code> serial console command, rendered by the device. Keys: c/m/s/n sort,
      p pause, + and - change the refresh.
    </p>
  </section>
</template>
