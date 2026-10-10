<script setup lang="ts">
import { computed, ref, useId } from 'vue'
import { confirmState, settleConfirm } from './useConfirm'
import { useModalFocus } from './useModalFocus'

// The one confirmation dialog of the app: useConfirm().confirm(...) fills it. Escape and the backdrop mean "no".
const titleId = useId()
const messageId = useId()
const panel = ref<HTMLElement | null>(null)

const open = computed({
  get: () => confirmState.pending !== null,
  set: (value: boolean) => {
    if (!value) settleConfirm(false)
  },
})
useModalFocus(open, panel)
</script>

<template>
  <Teleport to="body">
    <Transition name="nl-fade">
      <div v-if="confirmState.pending" class="nl-dialog-layer">
        <div class="nl-dialog-backdrop" @click="settleConfirm(false)" />
        <div
          ref="panel"
          class="nl-dialog"
          role="alertdialog"
          aria-modal="true"
          :aria-labelledby="titleId"
          :aria-describedby="messageId"
          tabindex="-1"
        >
          <div>
            <h2 :id="titleId">{{ confirmState.pending.title }}</h2>
            <p :id="messageId" class="nl-dialog-sub nl-confirm-text">{{ confirmState.pending.message }}</p>
          </div>
          <div class="nl-dialog-actions">
            <!-- The safe choice has the focus on open. -->
            <button type="button" class="nl-btn nl-btn-neutral" data-autofocus @click="settleConfirm(false)">Cancel</button>
            <button
              type="button"
              class="nl-btn"
              :class="confirmState.pending.tone === 'danger' ? 'nl-btn-solid-danger' : 'nl-btn-primary'"
              @click="settleConfirm(true)"
            >
              {{ confirmState.pending.confirmLabel ?? 'Confirm' }}
            </button>
          </div>
        </div>
      </div>
    </Transition>
  </Teleport>
</template>
