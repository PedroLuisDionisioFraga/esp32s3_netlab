<script setup>
import { ref, watch } from 'vue'
import { setLedAuto, setLedColor } from '../api'
import { hexToRgb, rgbToHex } from '../format'

const props = defineProps({
  led: { type: Object, default: null },
})
const emit = defineEmits(['changed'])

const color = ref('#000000')
const busy = ref(false)
const error = ref(null)

// Follow what the device reports, so a reload or a change from another browser shows up here.
watch(
  () => props.led,
  (led) => {
    if (led && !busy.value) {
      color.value = rgbToHex(led.r, led.g, led.b)
    }
  },
  { immediate: true },
)

async function send(action) {
  busy.value = true
  error.value = null
  try {
    await action()
    emit('changed')
  } catch (err) {
    error.value = err.message
  } finally {
    busy.value = false
  }
}

// Only the `change` event (picker closed) is used: `input` fires continuously while
// dragging and would flood a device that serves one request at a time.
function onPick(event) {
  color.value = event.target.value
  send(() => setLedColor(...hexToRgb(color.value)))
}

const turnOff = () => send(() => setLedColor(0, 0, 0))
const useAuto = () => send(() => setLedAuto())
</script>

<template>
  <section class="card">
    <h2>Onboard LED</h2>

    <div class="row">
      <input type="color" :value="color" :disabled="busy || !led" @change="onPick">
      <button :disabled="busy || !led" @click="turnOff">Off</button>
      <button :disabled="busy || !led || led.mode === 'auto'" @click="useAuto">Auto</button>
    </div>

    <p v-if="led" class="hint">
      <template v-if="led.mode === 'auto'">
        Showing the device status: blue booting, amber connecting, green online, red offline.
      </template>
      <template v-else>Manual color. Press Auto to show the device status again.</template>
    </p>
    <p v-if="error" class="error">{{ error }}</p>
  </section>
</template>
