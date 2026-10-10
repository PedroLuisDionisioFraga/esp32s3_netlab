import { reactive, watch } from 'vue'

// Theme, density and the collapsed sidebar: per-browser preferences, kept in localStorage and applied as attributes
// on <html> (tokens.css reads them). index.html applies the saved ones before the first paint.

export type ThemeChoice = 'system' | 'light' | 'dark'

interface Modes {
  theme: ThemeChoice
  compact: boolean
  rail: boolean
}

const KEY = 'netlab:ui'

function load(): Modes {
  const modes: Modes = { theme: 'system', compact: false, rail: false }
  try {
    const stored = JSON.parse(localStorage.getItem(KEY) ?? '{}') as Record<string, unknown>
    if (stored.theme === 'light' || stored.theme === 'dark') modes.theme = stored.theme
    if (stored.density === 'compact') modes.compact = true
    if (stored.rail === true) modes.rail = true
  } catch {
    /* storage blocked or garbage in it: the defaults */
  }
  return modes
}

const modes = reactive<Modes>(load())

function apply(): void {
  const root = document.documentElement
  if (modes.theme === 'system') delete root.dataset.theme
  else root.dataset.theme = modes.theme
  if (modes.compact) root.dataset.density = 'compact'
  else delete root.dataset.density
}

watch(
  modes,
  () => {
    apply()
    try {
      localStorage.setItem(
        KEY,
        JSON.stringify({ theme: modes.theme, density: modes.compact ? 'compact' : 'comfortable', rail: modes.rail }),
      )
    } catch {
      /* the choice still holds for this page view */
    }
  },
  { deep: true },
)

apply()

/** The preferences, as one reactive object shared by the whole app. */
export function useUiModes(): Modes {
  return modes
}
