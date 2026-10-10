<script setup>
import { computed } from 'vue'
import { getLink, getSystemInfo } from './api'
import ChatView from './components/ChatView.vue'
import DeviceCard from './components/DeviceCard.vue'
import HeaptopView from './components/HeaptopView.vue'
import LedCard from './components/LedCard.vue'
import LinkCard from './components/LinkCard.vue'
import RouterView from './components/RouterView.vue'
import { usePolling } from './usePolling'

const { data: info, error: infoError, refresh: refreshInfo } = usePolling(getSystemInfo, 2000)
const { data: link, error: linkError } = usePolling(getLink, 4000)

// No router: the device serves index.html for /chat, /heaptop and /router too, so the path picks the view.
const path = location.pathname.replace(/\/$/, '')
const isChat = path === '/chat'
const isHeaptop = path === '/heaptop'
const isRouter = path === '/router'

const reachable = computed(() => info.value !== null && infoError.value === null)
</script>

<template>
  <header class="topbar">
    <h1>Netlab</h1>
    <nav>
      <a href="/">Dashboard</a> | <a href="/chat">Chat</a> | <a href="/heaptop">Heaptop</a> |
      <a href="/router">Router lab</a>
    </nav>
    <span class="badge" :class="reachable ? 'ok' : 'bad'">
      {{ reachable ? 'Device reachable' : 'Device unreachable' }}
    </span>
  </header>

  <main v-if="isChat" class="grid">
    <ChatView />
  </main>

  <main v-else-if="isHeaptop" class="grid">
    <HeaptopView />
  </main>

  <main v-else-if="isRouter" class="grid">
    <RouterView />
  </main>

  <main v-else class="grid">
    <DeviceCard :info="info" :error="infoError" />
    <LinkCard :link="link" :error="linkError" />
    <LedCard :led="info?.led ?? null" @changed="refreshInfo" />
  </main>
</template>
