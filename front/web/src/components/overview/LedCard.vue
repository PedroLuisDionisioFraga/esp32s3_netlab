<script setup lang="ts">
import { ref, watch } from 'vue'
import { describeError, setLedAuto, setLedColor, type LedState } from '../../api'
import { hexToRgb, rgbToHex } from '../../format'
import NlCard from '../../ui/NlCard.vue'
import NlCardHead from '../../ui/NlCardHead.vue'
import NlPill from '../../ui/NlPill.vue'

const props = defineProps<{ led: LedState | null }>()
const emit = defineEmits<{ changed: [] }>()

const color = ref('#000000')
const busy = ref(false)
const error = ref('')

// Follow what the device reports, so a reload, or a change made from another browser, shows up here.
watch(
  () => props.led,
  (led) => {
    if (led && !busy.value) color.value = rgbToHex(led.r, led.g, led.b)
  },
  { immediate: true },
)

async function send(action: () => Promise<unknown>): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    await action()
    emit('changed')
  } catch (caught) {
    error.value = describeError(caught)
  } finally {
    busy.value = false
  }
}

// Only the `change` event (the picker closed) is used: `input` fires all the way through a drag, and a device that
// serves one request at a time would be flooded.
function onPick(event: Event): void {
  color.value = (event.target as HTMLInputElement).value
  const [r, g, b] = hexToRgb(color.value)
  void send(() => setLedColor(r, g, b))
}
</script>

<template>
  <NlCard>
    <template #head>
      <NlCardHead title="Onboard LED" subtitle="The RGB LED on the board">
        <template #actions>
          <NlPill v-if="led" :tone="led.mode === 'auto' ? 'info' : 'neutral'">
            {{ led.mode === 'auto' ? 'Showing status' : 'Manual colour' }}
          </NlPill>
        </template>
      </NlCardHead>
    </template>

    <div class="nl-led-row">
      <input
        type="color"
        class="nl-color"
        aria-label="LED colour"
        :value="color"
        :disabled="busy || !led"
        @change="onPick"
      />
      <button type="button" class="nl-btn nl-btn-neutral" :disabled="busy || !led" @click="send(() => setLedColor(0, 0, 0))">
        Off
      </button>
      <button
        type="button"
        class="nl-btn nl-btn-neutral"
        :disabled="busy || !led || led.mode === 'auto'"
        @click="send(() => setLedAuto())"
      >
        Auto
      </button>
    </div>

    <p v-if="led" class="nl-hint" style="margin-top: 12px">
      <template v-if="led.mode === 'auto'">
        Showing the device status: blue booting, purple setup, amber connecting, green online, red offline.
      </template>
      <template v-else>Manual colour. Press Auto to show the device status again.</template>
    </p>
    <p v-if="error" class="nl-notice nl-notice-error" role="alert" style="margin-top: 12px">{{ error }}</p>
  </NlCard>
</template>
