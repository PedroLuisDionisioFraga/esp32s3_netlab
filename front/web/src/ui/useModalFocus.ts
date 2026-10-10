import { getCurrentScope, nextTick, onScopeDispose, watch, type Ref } from 'vue'

const FOCUSABLE = [
  'a[href]',
  'button:not([disabled])',
  'input:not([disabled]):not([type="hidden"])',
  'select:not([disabled])',
  'textarea:not([disabled])',
  '[tabindex]:not([tabindex="-1"])',
].join(',')

// The dialogs that are open, innermost last: only the top one answers Escape and Tab.
const openStack: symbol[] = []

/**
 * Keyboard behaviour of a modal panel: when `open` turns true, the focus moves inside (to the element marked
 * `data-autofocus`, else the first control, else the panel itself); Tab and Shift+Tab cycle within the panel; Escape
 * sets `open` to false; when it closes, the focus goes back to `returnTo`, or to what had it before the dialog opened.
 */
export function useModalFocus(
  open: Ref<boolean>,
  panel: Ref<HTMLElement | null>,
  returnTo?: Ref<HTMLElement | null | undefined>,
): void {
  const token = Symbol('modal')
  let before: HTMLElement | null = null

  // Tab stops only: an element with tabindex -1 matches the selector but is not one.
  function controls(): HTMLElement[] {
    const root = panel.value
    if (!root) return []
    return Array.from(root.querySelectorAll<HTMLElement>(FOCUSABLE)).filter((element) => element.tabIndex >= 0)
  }

  function onKeydown(event: KeyboardEvent): void {
    if (openStack[openStack.length - 1] !== token) return

    if (event.key === 'Escape') {
      event.preventDefault()
      open.value = false
      return
    }
    if (event.key !== 'Tab') return

    const root = panel.value
    const items = controls()
    if (!root || items.length === 0) {
      event.preventDefault()
      root?.focus()
      return
    }
    const first = items[0] as HTMLElement
    const last = items[items.length - 1] as HTMLElement
    const active = document.activeElement
    // Clicking text inside the panel leaves the focus on the panel itself, which counts as "before the first".
    const outside = active === root || !root.contains(active)
    if (event.shiftKey && (active === first || outside)) {
      event.preventDefault()
      last.focus()
    } else if (!event.shiftKey && (active === last || outside)) {
      event.preventDefault()
      first.focus()
    }
  }

  function enter(): void {
    if (openStack.includes(token)) return
    before = document.activeElement instanceof HTMLElement ? document.activeElement : null
    openStack.push(token)
    document.addEventListener('keydown', onKeydown)
    void nextTick(() => {
      const root = panel.value
      if (!root) return
      const target = root.querySelector<HTMLElement>('[data-autofocus]') ?? controls()[0] ?? root
      target.focus()
    })
  }

  function leave(): void {
    const index = openStack.indexOf(token)
    if (index === -1) return
    openStack.splice(index, 1)
    document.removeEventListener('keydown', onKeydown)
    // The trigger may have left the page meanwhile (a button under a v-if): take the first one still connected,
    // or the focus would fall back to <body>.
    const candidates = [returnTo?.value, before]
    before = null
    void nextTick(() => candidates.find((element) => element?.isConnected && element !== document.body)?.focus())
  }

  watch(open, (isOpen) => (isOpen ? enter() : leave()), { immediate: true })
  if (getCurrentScope()) onScopeDispose(leave)
}
