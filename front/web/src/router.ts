import { createRouter, createWebHistory } from 'vue-router'
import { hasSession, isAuthRequired } from './session'
import type { IconName } from './ui/icons'

declare module 'vue-router' {
  interface RouteMeta {
    /** Page title: the header, the tab and the sidebar tooltip. */
    title?: string
    /** Reachable without a session (the login page). */
    public?: boolean
    /** 'blank' pages are drawn without the sidebar and header. */
    layout?: 'blank'
    /** Present = the page is in the sidebar. */
    nav?: { label: string; icon: IconName; order: number }
  }
}

// Paths without a file extension are answered with index.html by the device (rest_server.c), so a reload on any of
// these works and the links can be real URLs.
export const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: '/',
      name: 'overview',
      component: () => import('./pages/OverviewPage.vue'),
      meta: { title: 'Overview', nav: { label: 'Overview', icon: 'layout-dashboard', order: 10 } },
    },
    {
      path: '/memory',
      name: 'memory',
      component: () => import('./pages/MemoryPage.vue'),
      meta: { title: 'Memory', nav: { label: 'Memory', icon: 'activity', order: 20 } },
    },
    {
      path: '/network',
      name: 'network',
      component: () => import('./pages/NetworkPage.vue'),
      meta: { title: 'Network', nav: { label: 'Network', icon: 'wifi', order: 30 } },
    },
    {
      path: '/chat',
      name: 'chat',
      component: () => import('./pages/ChatPage.vue'),
      meta: { title: 'Chat', nav: { label: 'Chat', icon: 'message-square', order: 40 } },
    },
    {
      path: '/login',
      name: 'login',
      component: () => import('./pages/LoginPage.vue'),
      meta: { title: 'Sign in', public: true, layout: 'blank' },
    },
    { path: '/:pathMatch(.*)*', redirect: '/' },
  ],
})

// Everything but the login page needs a session, unless the firmware was built without a login.
router.beforeEach(async (to) => {
  if (to.meta.public) return true
  if (!(await isAuthRequired()) || hasSession()) return true
  return { path: '/login', query: { redirect: to.fullPath } }
})

router.afterEach((to) => {
  document.title = to.meta.title ? `${to.meta.title} · Netlab` : 'Netlab'
})
