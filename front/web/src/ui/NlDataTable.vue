<script setup lang="ts">
import { useId } from 'vue'
import NlIcon from './NlIcon.vue'
import type { TableColumn } from './types'

// Slots: `cell-<column key>` ({ row, value }) replaces a cell's content.
withDefaults(
  defineProps<{
    columns: readonly TableColumn[]
    rows: readonly Record<string, unknown>[]
    /** The key of the field that identifies a row (stable between refreshes, so rows keep their DOM). */
    rowKey: string
    /** The table's name, for a screen reader (visually hidden). */
    caption: string
    loading?: boolean
    error?: string | null
    emptyText?: string
  }>(),
  { error: null, emptyText: 'Nothing here yet' },
)

defineEmits<{ retry: [] }>()

const captionId = useId()

function display(value: unknown): string {
  return value === null || value === undefined || value === '' ? '—' : String(value)
}
</script>

<template>
  <!-- A scrollable region has to be reachable by keyboard, so it takes the focus. -->
  <div class="nl-table-wrap" role="region" :aria-labelledby="captionId" tabindex="0">
    <table class="nl-table" :aria-busy="loading ? 'true' : undefined">
      <caption :id="captionId" class="nl-visually-hidden">{{ caption }}</caption>
      <thead>
        <tr>
          <th v-for="column in columns" :key="column.key" scope="col" :class="{ 'is-text': column.text }">
            {{ column.label }}
          </th>
        </tr>
      </thead>
      <tbody>
        <tr v-if="loading">
          <td class="nl-table-state" :colspan="columns.length">Loading…</td>
        </tr>
        <tr v-else-if="error">
          <td class="nl-table-state" :colspan="columns.length">
            <span role="alert"><NlIcon name="triangle-alert" /> {{ error }}</span>
            <button type="button" class="nl-btn nl-btn-neutral" @click="$emit('retry')">Try again</button>
          </td>
        </tr>
        <tr v-else-if="rows.length === 0">
          <td class="nl-table-state" :colspan="columns.length">{{ emptyText }}</td>
        </tr>
        <template v-else>
          <tr v-for="(row, index) in rows" :key="String(row[rowKey] ?? index)">
            <td v-for="column in columns" :key="column.key" :class="{ 'is-text': column.text }">
              <slot :name="`cell-${column.key}`" :row="row" :value="row[column.key]">{{ display(row[column.key]) }}</slot>
            </td>
          </tr>
        </template>
      </tbody>
    </table>
  </div>
</template>
