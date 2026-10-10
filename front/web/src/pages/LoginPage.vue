<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { describeError } from '../api'
import { fetchAbout, hasSession, login, type About } from '../session'
import NlIcon from '../ui/NlIcon.vue'

const route = useRoute()
const router = useRouter()

const about = ref<About | null>(null)
const aboutError = ref('')
const username = ref('')
const password = ref('')
const reveal = ref(false)
const busy = ref(false)
const failure = ref('')
const userField = ref<HTMLInputElement | null>(null)

const notice = computed(() => (route.query.reason === 'expired' ? 'Your session ended. Sign in again.' : ''))
const canSubmit = computed(() => !busy.value && username.value.trim() !== '' && password.value !== '')

// Only a path on this site: a link to /login?redirect=https://elsewhere must not become an open redirect.
function destination(): string {
  const redirect = route.query.redirect
  return typeof redirect === 'string' && redirect.startsWith('/') && !redirect.startsWith('//') ? redirect : '/'
}

async function loadAbout(): Promise<void> {
  aboutError.value = ''
  try {
    about.value = await fetchAbout()
    // No login on this firmware, or already signed in: nothing to ask.
    if (!about.value.auth || hasSession()) await router.replace(destination())
  } catch (error) {
    aboutError.value = describeError(error)
  }
}

async function submit(): Promise<void> {
  if (!canSubmit.value) return
  busy.value = true
  failure.value = ''
  try {
    await login(username.value.trim(), password.value)
    await router.replace(destination())
  } catch (error) {
    failure.value = describeError(error)
    password.value = ''
  } finally {
    busy.value = false
  }
}

onMounted(() => {
  void loadAbout()
  userField.value?.focus()
})
</script>

<template>
  <div class="nl-login">
    <main class="nl-login-card">
      <div class="nl-login-brand">
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
        <div>
          <h1>Netlab</h1>
          <p class="nl-login-meta">
            <template v-if="about">{{ about.name }} {{ about.version }} · {{ about.hostname }}.local</template>
            <template v-else>Home network lab</template>
          </p>
        </div>
      </div>

      <p v-if="notice" class="nl-notice nl-notice-warn" role="status">
        <NlIcon name="info" /><span>{{ notice }}</span>
      </p>

      <div v-if="aboutError" class="nl-notice nl-notice-error" role="alert">
        <NlIcon name="triangle-alert" />
        <div class="nl-stack nl-gap-8">
          <p>Could not reach the lab. {{ aboutError }}</p>
          <div><button type="button" class="nl-btn nl-btn-neutral" @click="loadAbout">Try again</button></div>
        </div>
      </div>

      <form class="nl-login-form" novalidate @submit.prevent="submit">
        <div class="nl-field">
          <label for="login-user">User name</label>
          <input
            id="login-user"
            ref="userField"
            v-model="username"
            class="nl-input"
            autocomplete="username"
            autocapitalize="none"
            spellcheck="false"
            required
          />
        </div>

        <div class="nl-field">
          <label for="login-password">Password</label>
          <div class="nl-input-group">
            <input
              id="login-password"
              v-model="password"
              class="nl-input"
              :type="reveal ? 'text' : 'password'"
              autocomplete="current-password"
              required
            />
            <button
              type="button"
              class="nl-icon-btn"
              :aria-label="reveal ? 'Hide the password' : 'Show the password'"
              :aria-pressed="reveal"
              @click="reveal = !reveal"
            >
              <NlIcon :name="reveal ? 'eye-off' : 'eye'" />
            </button>
          </div>
        </div>

        <p v-if="failure" class="nl-notice nl-notice-error" role="alert">
          <NlIcon name="triangle-alert" /><span>{{ failure }}</span>
        </p>

        <button type="submit" class="nl-btn nl-btn-primary" :disabled="!canSubmit" :aria-busy="busy ? 'true' : undefined">
          {{ busy ? 'Signing in…' : 'Sign in' }}
        </button>
      </form>

      <p class="nl-hint">
        <NlIcon name="lock" /> The page talks to the lab over plain HTTP: use it on your home network only.
      </p>
    </main>
  </div>
</template>
