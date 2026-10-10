<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { RouterLink, useRoute, useRouter } from 'vue-router'
import { connection } from '../connection'
import { logout, sessionActive } from '../session'
import NlIcon from './NlIcon.vue'
import NlLiveBadge from './NlLiveBadge.vue'
import NlSegmented from './NlSegmented.vue'
import { useModalFocus } from './useModalFocus'
import { useUiModes, type ThemeChoice } from './useUiModes'

// The frame of every signed-in page: a sidebar (a collapsible rail on a desktop, an off-canvas drawer under 1024px),
// a header with the connection state and the preferences, and the page in <main>.
defineSlots<{ default(): unknown }>()

const route = useRoute()
const router = useRouter()
const modes = useUiModes()

const nav = computed(() =>
  router
    .getRoutes()
    .flatMap((entry) => (entry.meta.nav ? [{ path: entry.path, title: entry.meta.title ?? '', ...entry.meta.nav }] : []))
    .sort((a, b) => a.order - b.order),
)

// ---- Drawer (narrow screens) ----
const narrowQuery = window.matchMedia('(max-width: 1023.98px)')
const narrow = ref(narrowQuery.matches)
const drawerOpen = ref(false)
const sidebar = ref<HTMLElement | null>(null)
const menuButton = ref<HTMLElement | null>(null)

function onMediaChange(event: MediaQueryListEvent): void {
  narrow.value = event.matches
  if (!event.matches) drawerOpen.value = false
}
onMounted(() => narrowQuery.addEventListener('change', onMediaChange))
onBeforeUnmount(() => narrowQuery.removeEventListener('change', onMediaChange))

// The drawer behaves as a modal dialog while it is open on a narrow screen: focus trap, Escape, focus back to the button.
const drawerModal = computed({
  get: () => narrow.value && drawerOpen.value,
  set: (value: boolean) => {
    if (!value) drawerOpen.value = false
  },
})
useModalFocus(drawerModal, sidebar, menuButton)

// ---- Page changes: close the drawer, and put the focus on the new page for keyboard and screen-reader users ----
const main = ref<HTMLElement | null>(null)
watch(
  () => route.fullPath,
  () => {
    drawerOpen.value = false
    void nextTick(() => main.value?.focus({ preventScroll: true }))
  },
)

// ---- Preferences popover ----
const settingsOpen = ref(false)
const settingsRoot = ref<HTMLElement | null>(null)
const settingsButton = ref<HTMLElement | null>(null)

function onDocumentPointer(event: PointerEvent): void {
  if (settingsOpen.value && !settingsRoot.value?.contains(event.target as Node)) settingsOpen.value = false
}

function onDocumentKey(event: KeyboardEvent): void {
  if (event.key === 'Escape' && settingsOpen.value) {
    settingsOpen.value = false
    settingsButton.value?.focus()
  }
}

onMounted(() => {
  document.addEventListener('pointerdown', onDocumentPointer)
  document.addEventListener('keydown', onDocumentKey)
})
onBeforeUnmount(() => {
  document.removeEventListener('pointerdown', onDocumentPointer)
  document.removeEventListener('keydown', onDocumentKey)
})

const themeOptions = [
  { value: 'system', label: 'System' },
  { value: 'light', label: 'Light' },
  { value: 'dark', label: 'Dark' },
]
const theme = computed<string>({
  get: () => modes.theme,
  set: (value) => {
    if (value === 'system' || value === 'light' || value === 'dark') modes.theme = value as ThemeChoice
  },
})

const connectionTone = computed(() =>
  connection.status === 'online' ? 'live' : connection.status === 'offline' ? 'down' : 'warn',
)
const connectionText = computed(() =>
  connection.status === 'online' ? 'Connected' : connection.status === 'offline' ? 'Offline' : 'Connecting…',
)

async function signOut(): Promise<void> {
  await logout()
  await router.push({ path: '/login' })
}
</script>

<template>
  <a class="nl-skip" href="#main" @click.prevent="main?.focus()">Skip to content</a>

  <div class="nl-shell" :class="{ 'is-rail': modes.rail }">
    <div
      id="sidebar"
      ref="sidebar"
      class="nl-sidebar"
      :class="{ 'is-open': drawerOpen }"
      :role="drawerModal ? 'dialog' : undefined"
      :aria-modal="drawerModal ? 'true' : undefined"
      :aria-label="drawerModal ? 'Menu' : undefined"
      :tabindex="drawerModal ? -1 : undefined"
      :inert="narrow && !drawerOpen ? true : undefined"
    >
      <RouterLink to="/" class="nl-brand" aria-label="Netlab, home">
        <svg class="nl-brand-mark" viewBox="0 0 32 32" aria-hidden="true">
          <rect width="32" height="32" rx="7" fill="#0f766e" />
          <path
            d="M6 21a14 14 0 0 1 20 0M10 17a9 9 0 0 1 12 0M14 13a4 4 0 0 1 4 0"
            stroke="#ffffff"
            stroke-width="2.4"
            fill="none"
            stroke-linecap="round"
          />
        </svg>
        <span class="nl-brand-name">Netlab</span>
      </RouterLink>

      <nav aria-label="Main">
        <ul class="nl-nav">
          <li v-for="item in nav" :key="item.path">
            <RouterLink :to="item.path" class="nl-nav-link" :title="item.title">
              <NlIcon :name="item.icon" />
              <span class="nl-side-label">{{ item.label }}</span>
            </RouterLink>
          </li>
        </ul>
      </nav>

      <div class="nl-sidebar-foot">
        <button
          type="button"
          class="nl-side-btn nl-rail-btn"
          :aria-pressed="modes.rail"
          :title="modes.rail ? 'Expand the menu' : 'Collapse the menu'"
          @click="modes.rail = !modes.rail"
        >
          <NlIcon name="panel-left" />
          <span class="nl-side-label">{{ modes.rail ? 'Expand menu' : 'Collapse menu' }}</span>
        </button>
        <button v-if="sessionActive" type="button" class="nl-side-btn" title="Sign out" @click="signOut">
          <NlIcon name="log-out" />
          <span class="nl-side-label">Sign out</span>
        </button>
      </div>
    </div>
    <div v-if="drawerModal" class="nl-scrim" @click="drawerOpen = false" />

    <div class="nl-body">
      <header class="nl-header">
        <button
          ref="menuButton"
          type="button"
          class="nl-icon-btn nl-menu-btn"
          aria-label="Open the menu"
          aria-controls="sidebar"
          :aria-expanded="drawerOpen"
          @click="drawerOpen = true"
        >
          <NlIcon name="menu" />
        </button>
        <span class="nl-header-title">{{ route.meta.title }}</span>
        <NlLiveBadge :tone="connectionTone">{{ connectionText }}</NlLiveBadge>

        <div ref="settingsRoot" class="nl-pop-wrap">
          <button
            ref="settingsButton"
            type="button"
            class="nl-icon-btn"
            aria-label="Preferences"
            aria-haspopup="true"
            :aria-expanded="settingsOpen"
            @click="settingsOpen = !settingsOpen"
          >
            <NlIcon name="sliders-horizontal" />
          </button>
          <div v-if="settingsOpen" class="nl-pop" role="group" aria-label="Preferences">
            <h2>Preferences</h2>
            <div class="nl-field">
              <span class="nl-label">Theme</span>
              <NlSegmented v-model="theme" :options="themeOptions" label="Theme" />
            </div>
            <label class="nl-check">
              <input v-model="modes.compact" type="checkbox" />
              Compact layout
            </label>
          </div>
        </div>
      </header>

      <main id="main" ref="main" class="nl-main" tabindex="-1">
        <slot />
      </main>
    </div>
  </div>
</template>
