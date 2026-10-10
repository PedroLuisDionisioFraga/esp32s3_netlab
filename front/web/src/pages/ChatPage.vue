<script setup lang="ts">
import { nextTick, ref } from 'vue'
import { describeError, sendChat } from '../api'
import NlCard from '../ui/NlCard.vue'
import NlCardHead from '../ui/NlCardHead.vue'
import NlIcon from '../ui/NlIcon.vue'
import NlPageHeader from '../ui/NlPageHeader.vue'

// A placeholder conversation: the lab echoes the message back. It exercises the authenticated POST end to end.
interface Message {
  from: 'me' | 'lab'
  text: string
}

const messages = ref<Message[]>([])
const text = ref('')
const busy = ref(false)
const error = ref('')
const list = ref<HTMLElement | null>(null)

async function scrollDown(): Promise<void> {
  await nextTick()
  list.value?.scrollTo({ top: list.value.scrollHeight })
}

async function send(): Promise<void> {
  const message = text.value.trim()
  if (!message || busy.value) return
  busy.value = true
  error.value = ''
  messages.value.push({ from: 'me', text: message })
  text.value = ''
  void scrollDown()
  try {
    const reply = await sendChat(message)
    messages.value.push({ from: 'lab', text: reply.message })
    void scrollDown()
  } catch (caught) {
    error.value = describeError(caught)
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <div class="nl-stack">
    <NlPageHeader title="Chat" subtitle="Send a message to the lab" />

    <NlCard>
      <template #head>
        <NlCardHead title="Conversation" subtitle="The lab answers with the same words" />
      </template>

      <ul ref="list" class="nl-chat-list" aria-label="Messages" aria-live="polite">
        <li v-if="messages.length === 0" class="nl-hint">No messages yet.</li>
        <li v-for="(message, index) in messages" :key="index" class="nl-chat-msg" :class="{ 'is-me': message.from === 'me' }">
          <span class="nl-chat-who">{{ message.from === 'me' ? 'You' : 'Lab' }}</span>
          {{ message.text }}
        </li>
      </ul>

      <p v-if="error" class="nl-notice nl-notice-error" role="alert" style="margin-bottom: 12px">
        <NlIcon name="triangle-alert" /><span>{{ error }}</span>
      </p>

      <form class="nl-chat-form" @submit.prevent="send">
        <input v-model="text" class="nl-input" type="text" aria-label="Message" placeholder="Message" autocomplete="off" :disabled="busy" />
        <button type="submit" class="nl-btn nl-btn-primary" :disabled="busy || !text.trim()">
          <NlIcon name="send" /> Send
        </button>
      </form>
    </NlCard>
  </div>
</template>
