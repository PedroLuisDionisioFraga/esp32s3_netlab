<script setup>
import { ref } from 'vue'
import { sendChat } from '../api'

const messages = ref([])
const text = ref('')
const busy = ref(false)
const error = ref(null)

async function send() {
  const message = text.value.trim()
  if (!message || busy.value) {
    return
  }
  busy.value = true
  error.value = null
  messages.value.push({ from: 'me', text: message })
  text.value = ''
  try {
    const reply = await sendChat(message)
    messages.value.push({ from: 'device', text: reply.message })
  } catch (err) {
    error.value = err.message
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <section class="card">
    <h2>Chat</h2>

    <p v-for="(m, i) in messages" :key="i" class="hint">
      <strong>{{ m.from === 'me' ? 'You' : 'Device' }}:</strong> {{ m.text }}
    </p>
    <p v-if="!messages.length" class="hint">Send a message to the device.</p>
    <p v-if="error" class="hint">{{ error }}</p>

    <form class="row" @submit.prevent="send">
      <input v-model="text" type="text" placeholder="Message" :disabled="busy">
      <button type="submit" :disabled="busy || !text.trim()">Send</button>
    </form>
  </section>
</template>
