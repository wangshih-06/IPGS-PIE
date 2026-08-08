import type { Point } from './growthData'

export type ExportKind = 'csv' | 'json'
export type GrowthExport = { filename: string; mimeType: string; text: string }
export type MetricFrame = Omit<Point, 'plantState'>

const csvHeader = ['step', 'age_years', 'life_stage', 'height', 'total_branch_length', 'branch_count', 'leaf_count', 'canopy_width']

export function escapeCsvField(value: unknown) {
  const field = String(value ?? '')
  return /[",\r\n]/.test(field) ? `"${field.replaceAll('"', '""')}"` : field
}

export function toMetricFrames(frames: Point[]): MetricFrame[] {
  return frames.map(({ plantState: _plantState, ...frame }) => frame)
}

export function buildGrowthCsv(frames: Point[]): string {
  const rows = [
    csvHeader.join(','),
    ...toMetricFrames(frames).map(frame => [
      frame.step,
      frame.age.toFixed(6),
      frame.lifeStage,
      frame.height.toFixed(6),
      frame.totalBranchLength.toFixed(6),
      frame.branchCount,
      frame.leafCount,
      frame.canopyWidth.toFixed(6),
    ].map(escapeCsvField).join(',')),
  ]
  return `\uFEFF${rows.join('\r\n')}`
}

export function buildGrowthJson(frames: Point[], exportedAt = new Date().toISOString()): string {
  return JSON.stringify({
    schema: 'plantsim.growth_metrics.frontend/v1',
    exportedAt,
    frameCount: frames.length,
    frames: toMetricFrames(frames),
  }, null, 2)
}

export function makeGrowthExport(kind: ExportKind, frames: Point[], exportedAt?: string): GrowthExport {
  if (kind === 'csv') return {
    filename: 'plant-growth-metrics.csv',
    mimeType: 'text/csv;charset=utf-8',
    text: buildGrowthCsv(frames),
  }
  return {
    filename: 'plant-growth-metrics.json',
    mimeType: 'application/json',
    text: buildGrowthJson(frames, exportedAt),
  }
}

export function downloadGrowthExport(file: GrowthExport) {
  const url = URL.createObjectURL(new Blob([file.text], { type: file.mimeType }))
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.download = file.filename
  anchor.click()
  window.setTimeout(() => URL.revokeObjectURL(url), 0)
}
