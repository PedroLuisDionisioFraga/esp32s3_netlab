import { describe, expect, it } from 'vitest'
import {
  NO_TASK_FILTERS,
  activeFilterCount,
  countByState,
  filterTasks,
  parseTaskFilters,
  sortTasks,
  taskKeys,
  type MemoryTask,
  type TaskFilters,
} from './memory'

function task(partial: Partial<MemoryTask> & { name: string }): MemoryTask {
  return { state: 'blocked', prio: 1, core: null, cpu10: null, hwm: 1000, ...partial }
}

const httpd = task({ name: 'httpd', state: 'blocked', prio: 5, core: 0, cpu10: 30, hwm: 2000 })
const idle = task({ name: 'IDLE0', state: 'ready', prio: 0, core: 0, cpu10: 900, hwm: 800 })
const worker = task({ name: 'worker', state: 'running', prio: 3, core: 1, cpu10: null, hwm: 100, leak: true })
const wifi = task({ name: 'wifi', state: 'suspended', prio: 23, core: null, cpu10: 0, hwm: 500 })
const all = [httpd, idle, worker, wifi]

const filters = (over: Partial<TaskFilters>): TaskFilters => ({ ...NO_TASK_FILTERS, states: [], ...over })

describe('filterTasks', () => {
  it('shows everything with no filter', () => {
    expect(filterTasks(all, filters({}), 600)).toEqual(all)
  })

  it('matches the name in any case', () => {
    expect(filterTasks(all, filters({ query: 'id' }), 0)).toEqual([idle])
    expect(filterTasks(all, filters({ query: ' I ' }), 0)).toEqual([idle, wifi])
  })

  it('filters by state, any of the chosen', () => {
    expect(filterTasks(all, filters({ states: ['blocked', 'running'] }), 0)).toEqual([httpd, worker])
  })

  it('keeps the tasks under the stack floor, and none when the check is off', () => {
    expect(filterTasks(all, filters({ onlyTight: true }), 600)).toEqual([worker, wifi])
    expect(filterTasks(all, filters({ onlyTight: true }), 0)).toEqual([])
  })

  it('keeps the tasks that used CPU', () => {
    expect(filterTasks(all, filters({ onlyActive: true }), 0)).toEqual([httpd, idle])
  })

  it('filters by core and by leak suspicion', () => {
    expect(filterTasks(all, filters({ core: 1 }), 0)).toEqual([worker])
    expect(filterTasks(all, filters({ onlyLeaks: true }), 0)).toEqual([worker])
  })

  it('combines filters', () => {
    expect(filterTasks(all, filters({ query: 'w', states: ['suspended'] }), 0)).toEqual([wifi])
  })
})

describe('countByState', () => {
  it('counts every state', () => {
    expect(countByState(all)).toEqual({ running: 1, ready: 1, blocked: 1, suspended: 1, deleted: 0, unknown: 0 })
  })
})

describe('activeFilterCount', () => {
  it('counts the filters that are on', () => {
    expect(activeFilterCount(filters({}))).toBe(0)
    expect(activeFilterCount(filters({ query: '   ' }))).toBe(0)
    expect(activeFilterCount(filters({ query: 'x', states: ['ready'], onlyTight: true }))).toBe(3)
    expect(activeFilterCount(filters({ core: 0 }))).toBe(1)
  })
})

describe('taskKeys', () => {
  it('numbers tasks that share a name, in snapshot order', () => {
    const first = task({ name: 'ipc' })
    const second = task({ name: 'ipc' })
    const other = task({ name: 'main' })
    const keys = taskKeys([first, other, second])
    expect(keys.get(first)).toBe('ipc#1')
    expect(keys.get(second)).toBe('ipc#2')
    expect(keys.get(other)).toBe('main#1')
  })
})

describe('sortTasks', () => {
  it('puts the busiest first and tasks without a reading last', () => {
    expect(sortTasks(all, 'cpu')).toEqual([idle, httpd, wifi, worker])
  })

  it('puts the tightest stack first', () => {
    expect(sortTasks(all, 'stack')).toEqual([worker, wifi, idle, httpd])
  })

  it('sorts by name', () => {
    expect(sortTasks(all, 'name').map((item) => item.name)).toEqual(['httpd', 'IDLE0', 'wifi', 'worker'])
  })

  it('puts the biggest heap holder first, and tasks without the reading last', () => {
    const heavy = task({ name: 'a', heap: 500 })
    const light = task({ name: 'b', heap: 100 })
    const unknown = task({ name: 'c' })
    expect(sortTasks([unknown, light, heavy], 'heap')).toEqual([heavy, light, unknown])
  })

  it('does not change the list it is given', () => {
    const copy = [...all]
    sortTasks(all, 'name')
    expect(all).toEqual(copy)
  })
})

describe('parseTaskFilters', () => {
  it('has no filters for nothing or for garbage', () => {
    expect(parseTaskFilters(null)).toEqual(NO_TASK_FILTERS)
    expect(parseTaskFilters('{not json')).toEqual(NO_TASK_FILTERS)
    expect(parseTaskFilters('42')).toEqual(NO_TASK_FILTERS)
  })

  it('reads what was saved', () => {
    const saved = filters({ query: 'wifi', states: ['ready'], onlyTight: true, core: 1 })
    expect(parseTaskFilters(JSON.stringify(saved))).toEqual(saved)
  })

  it('drops what is the wrong shape, field by field', () => {
    const parsed = parseTaskFilters(
      JSON.stringify({ query: 7, states: ['ready', 'nope', 3], onlyTight: 'yes', core: -1, onlyActive: true }),
    )
    expect(parsed).toEqual(filters({ states: ['ready'], onlyActive: true }))
  })

  it('cuts an overlong query', () => {
    expect(parseTaskFilters(JSON.stringify({ query: 'x'.repeat(200) })).query).toHaveLength(64)
  })
})
