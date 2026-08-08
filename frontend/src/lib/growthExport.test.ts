import { describe, expect, it } from 'vitest'
import { buildGrowthCsv, buildGrowthJson, escapeCsvField, makeGrowthExport, toMetricFrames } from './growthExport'
import type { Point } from './growthData'

const frames: Point[] = [{
  step: 1,
  age: 1.25,
  lifeStage: 'Seedling, "quoted"\nline',
  height: 1.2,
  totalBranchLength: 2.3,
  branchCount: 3,
  leafCount: 4,
  canopyWidth: 5.6,
  plantState: { schema: 'plantsim.skeleton', skeleton: { nodes: [{ id: 1, parentId: -1, position: [0, 0, 0] }] } },
}]

describe('growth exports', () => {
  it('escapes commas, quotes, and line breaks for RFC-style CSV fields', () => {
    expect(escapeCsvField('plain')).toBe('plain')
    expect(escapeCsvField('left,right')).toBe('"left,right"')
    expect(escapeCsvField('say "hi"')).toBe('"say ""hi"""')
    expect(escapeCsvField('first\nsecond')).toBe('"first\nsecond"')
  })

  it('creates a BOM-prefixed CSV with every column safely escaped', () => {
    const csv = buildGrowthCsv(frames)
    expect(csv.startsWith('\uFEFFstep,age_years,life_stage')).toBe(true)
    expect(csv).toContain('"Seedling, ""quoted""\nline"')
    expect(csv.split('\r\n')).toHaveLength(2)
  })

  it('exports lightweight metric JSON without plant-state snapshots', () => {
    const metrics = toMetricFrames(frames)
    expect(metrics[0]).not.toHaveProperty('plantState')
    const json = buildGrowthJson(frames, '2026-08-08T00:00:00.000Z')
    const parsed = JSON.parse(json)
    expect(parsed).toMatchObject({ schema: 'plantsim.growth_metrics.frontend/v1', frameCount: 1 })
    expect(parsed.frames[0]).not.toHaveProperty('plantState')
    expect(json).not.toContain('plantsim.skeleton')
  })

  it('provides file metadata for both download formats', () => {
    expect(makeGrowthExport('csv', frames).filename).toBe('plant-growth-metrics.csv')
    expect(makeGrowthExport('json', frames, '2026-08-08T00:00:00.000Z').mimeType).toBe('application/json')
  })
})
