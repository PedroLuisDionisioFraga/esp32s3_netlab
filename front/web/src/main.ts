import { createApp } from 'vue'
import App from './App.vue'
import { onSessionExpired } from './api'
import { router } from './router'
import './styles/fonts.css'
import './styles/tokens.css'
import './styles/base.css'
import './styles/shell.css'
import './styles/components.css'
import './styles/memory.css'

// Any 401 from the device means the session is gone (it ended, or the device restarted and forgot it): back to the
// login, which sends the user on to the page they were on.
onSessionExpired(() => {
  if (router.currentRoute.value.path === '/login') return
  void router.push({ path: '/login', query: { redirect: router.currentRoute.value.fullPath, reason: 'expired' } })
})

createApp(App).use(router).mount('#app')
