export type PillTone = 'success' | 'warning' | 'error' | 'neutral' | 'info'
export type LiveTone = 'live' | 'warn' | 'down'

export interface SegmentedOption {
  value: string
  label: string
}

export interface TableColumn {
  key: string
  label: string
  /** Names and prose: left aligned. Numbers stay right aligned so they line up. */
  text?: boolean
}
