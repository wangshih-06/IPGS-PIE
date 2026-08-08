export type Metric = 'height' | 'totalBranchLength' | 'branchCount' | 'leafCount' | 'canopyWidth'
export type Vector3 = [number, number, number]

export type PlantSnapshot = {
  schema?: string
  plant?: { name?: string; species?: string; lifeStage?: string }
  skeleton?: {
    nodes?: Array<{ id: number; parentId: number; position: Vector3; radius?: number; active?: boolean; type?: string }>
    leaves?: Array<{ id: number; parentNodeId: number; position: Vector3; size?: [number, number]; active?: boolean }>
  }
}

export type Point = {
  step: number
  age: number
  lifeStage: string
  height: number
  totalBranchLength: number
  branchCount: number
  leafCount: number
  canopyWidth: number
  plantState?: PlantSnapshot
}

export type Stage = { key: string; label: string; age: number }

export const metricKeys: Metric[] = ['height', 'totalBranchLength', 'branchCount', 'leafCount', 'canopyWidth']

const stageLabels: Record<string, string> = {
  seedling: '\u5e7c\u82d7',
  vegetative: '\u8425\u517b\u751f\u957f',
  mature: '\u6210\u719f',
  completed: '\u5b8c\u6210',
  senescent: '\u8001\u5316',
}

export function stageTitle(stage: Stage) {
  return stageLabels[stage.key] ?? stage.label
}

export function asNumber(value: unknown, fallback = 0) {
  return typeof value === 'number' && Number.isFinite(value) ? value : fallback
}

function asRecord(value: unknown): Record<string, unknown> | null {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as Record<string, unknown> : null
}

function stageKey(value: string) {
  return value.trim().toLowerCase().replace(/[^a-z0-9]+/g, '-') || 'unknown'
}

export function offlineFrames(): Point[] {
  return Array.from({ length: 61 }, (_, step) => {
    const age = step / 2
    const growth = 1 / (1 + Math.exp(-(age - 4.5) * 0.56))
    return {
      step,
      age,
      lifeStage: age < 0.5 ? 'Seedling' : age < 3 ? 'Vegetative' : age < 20 ? 'Mature' : 'Senescent',
      height: +(0.15 + 8.6 * growth).toFixed(3),
      totalBranchLength: +(0.2 + 112 * growth ** 1.18).toFixed(3),
      branchCount: Math.round(2 + 174 * growth ** 1.45),
      leafCount: Math.round(8 + 2630 * growth ** 1.7),
      canopyWidth: +(0.4 + 7.2 * growth).toFixed(3),
    }
  })
}

export function stageListFromFrames(frames: Point[]): Stage[] {
  const stages: Stage[] = []
  let previous = ''
  for (const frame of frames) {
    const key = stageKey(frame.lifeStage)
    if (key === previous) continue
    stages.push({ key, label: frame.lifeStage, age: frame.age })
    previous = key
  }
  return stages
}

export function parseStages(value: unknown, frames: Point[]): Stage[] {
  if (Array.isArray(value)) {
    const parsed = value.map((item): Stage | null => {
      const record = asRecord(item)
      if (!record) return null
      const key = typeof record.key === 'string' ? record.key.trim() : ''
      const label = typeof record.label === 'string' ? record.label.trim() : key
      const age = asNumber(record.age, Number.NaN)
      return key && label && Number.isFinite(age) ? { key, label, age } : null
    }).filter((stage): stage is Stage => stage !== null).sort((a, b) => a.age - b.age)
    if (parsed.length) return parsed
  }
  return stageListFromFrames(frames)
}

export function toSnapshot(value: unknown): PlantSnapshot | undefined {
  const snapshot = asRecord(value) as PlantSnapshot | null
  return snapshot?.schema === 'plantsim.skeleton' && Array.isArray(snapshot.skeleton?.nodes) ? snapshot : undefined
}

export function toPoint(value: unknown, fallbackStep = 0): Point | null {
  const record = asRecord(value)
  if (!record) return null
  const metrics = asRecord(record.metrics) ?? record
  const age = asNumber(record.age, asNumber(metrics.age, Number.NaN))
  if (!Number.isFinite(age)) return null
  const snapshot = toSnapshot(record.plantState)
  return {
    step: asNumber(record.step, fallbackStep),
    age,
    lifeStage: String(record.lifeStage ?? metrics.lifeStage ?? snapshot?.plant?.lifeStage ?? 'Seedling'),
    height: asNumber(metrics.height),
    totalBranchLength: asNumber(metrics.totalBranchLength),
    branchCount: asNumber(metrics.branchCount),
    leafCount: asNumber(metrics.leafCount),
    canopyWidth: asNumber(metrics.canopyWidth),
    plantState: snapshot,
  }
}

/** Sort frames chronologically and keep the last copy of any duplicated simulation age. */
export function normalizeFrames(frames: Point[]): Point[] {
  const ordered = [...frames].sort((a, b) => a.age - b.age || a.step - b.step)
  return ordered.reduce<Point[]>((result, frame) => {
    const previous = result.at(-1)
    if (previous && Math.abs(previous.age - frame.age) < 0.0001) result[result.length - 1] = frame
    else result.push(frame)
    return result
  }, [])
}

/** Find a frame in O(log n); recorded frames are normalized chronologically before use. */
export function findNearest(frames: Point[], target: number) {
  if (!frames.length) return null
  if (target <= frames[0].age) return frames[0]
  const last = frames.at(-1)!
  if (target >= last.age) return last
  let low = 0
  let high = frames.length - 1
  while (low <= high) {
    const middle = Math.floor((low + high) / 2)
    if (frames[middle].age < target) low = middle + 1
    else high = middle - 1
  }
  const before = frames[Math.max(0, high)]
  const after = frames[Math.min(frames.length - 1, low)]
  return Math.abs(before.age - target) <= Math.abs(after.age - target) ? before : after
}

/**
 * Insert or replace a streamed frame while retaining the complete recorded timeline.
 * Seeking backwards must not discard later snapshots that are already available for replay.
 */
export function appendFrame(frames: Point[], frame: Point): Point[] {
  const next = [...frames]
  if (!next.length) return [frame]
  let low = 0
  let high = next.length
  while (low < high) {
    const middle = Math.floor((low + high) / 2)
    if (next[middle].age < frame.age) low = middle + 1
    else high = middle
  }
  const candidates = [low - 1, low].filter(index => index >= 0 && index < next.length)
  const duplicate = candidates.find(index => Math.abs(next[index].age - frame.age) < 0.0001)
  if (duplicate !== undefined) next[duplicate] = frame
  else next.splice(low, 0, frame)
  return next
}

export function sample<T>(items: T[], max: number): T[] {
  if (items.length <= max || max < 2) return [...items]
  const step = (items.length - 1) / (max - 1)
  return Array.from({ length: max }, (_, index) => items[Math.round(index * step)])
}

export function makePath(items: Point[], key: Metric, maxAge: number, maxValue: number) {
  const endAge = Math.max(0.001, maxAge)
  const top = Math.max(1, maxValue)
  return items.map((item, index) => {
    const x = (item.age / endAge * 1000).toFixed(2)
    const y = (228 - item[key] / top * 198).toFixed(2)
    return `${index ? 'L' : 'M'} ${x} ${y}`
  }).join(' ')
}
