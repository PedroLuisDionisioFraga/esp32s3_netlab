import { reactive, readonly } from 'vue'

export interface ConfirmOptions {
  title: string
  message: string
  /** Default "Confirm". */
  confirmLabel?: string
  /** 'danger' draws the confirm button in the error colour. Default 'primary'. */
  tone?: 'primary' | 'danger'
}

interface Pending extends ConfirmOptions {
  resolve: (confirmed: boolean) => void
}

const state = reactive<{ pending: Pending | null }>({ pending: null })

/** Read by NlConfirmDialog, which shows the question that is waiting. */
export const confirmState = readonly(state)

export function settleConfirm(confirmed: boolean): void {
  const pending = state.pending
  state.pending = null
  pending?.resolve(confirmed)
}

/** Asks a yes/no question in a dialog. Resolves true for the confirm button, false for anything else. */
export function useConfirm() {
  function confirm(options: ConfirmOptions): Promise<boolean> {
    // A second question while one is open cancels the first: there is only one dialog.
    settleConfirm(false)
    return new Promise((resolve) => {
      state.pending = { ...options, resolve }
    })
  }
  return { confirm }
}
