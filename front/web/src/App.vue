<script setup lang="ts">
import { computed } from 'vue'
import { RouterView, useRoute } from 'vue-router'
import NlAppShell from './ui/NlAppShell.vue'
import NlConfirmDialog from './ui/NlConfirmDialog.vue'
import NlToastHost from './ui/NlToastHost.vue'

// The login page is drawn on its own; every other page sits inside the shell (sidebar and header).
const route = useRoute()
const blank = computed(() => route.meta.layout === 'blank')
</script>

<template>
  <RouterView v-if="blank" />
  <NlAppShell v-else>
    <RouterView />
  </NlAppShell>

  <NlToastHost />
  <NlConfirmDialog />
</template>
