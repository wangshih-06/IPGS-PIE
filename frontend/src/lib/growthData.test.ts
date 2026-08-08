import { describe, expect, it } from 'vitest'
import {
  appendFrame,
  makePath,
  normalizeFrames,
  offlineFrames,
  parseStages,
  sample,
  stageListFromFrames,
  toPoint,
  type Point,
} from './growthData'

function frame(age: number, lifeStage = 'Seedling', step = age * 10): Point {
  return {
    step,
    age,
    lifeStage,
    height: age + 1,
    totalBranchLength: age + 2,
    branchCount: Math.round(age + 3),
    leafCount: Math.round(age + 4),
    canopyWidth: age + 5,
  }
}

describe('growth data helpers', () => {
  it('creates a chronological offline dataset with stable endpoints', () => {
    const frames = offlineFrames()
    expect(frames).toHaveLength(61)
    expect(frames[0].age).toBe(0)
    expect(frames.at(-1)?.age).toBe(30)
    expect(frames.every((item, index) => index === 0 || item.age > frames[index - 1].age)).toBe(true)
  })

  it('derives life-stage transitions and falls back when server stages are invalid', () => {
    const frames = [frame(0, 'Seedling'), frame(1, 'Seedling'), frame(2, 'Vegetative'), frame(4, 'Mature')]
    expect(stageListFromFrames(frames)).toEqual([
      { key: 'seedling', label: 'Seedling', age: 0 },
      { key: 'vegetative', label: 'Vegetative', age: 2 },
      { key: 'mature', label: 'Mature', age: 4 },
    ])
    expect(parseStages([{ key: '', label: 'bad', age: 0 }], frames)).toEqual(stageListFromFrames(frames))
    expect(parseStages([{ key: 'mature', label: 'Mature', age: 4 }], frames)).toEqual([{ key: 'mature', label: 'Mature', age: 4 }])
  })

  it('normalizes unordered duplicate frames and keeps the latest copy at each age', () => {
    const first = frame(1, 'Seedling', 10)
    const replacement = { ...frame(1, 'Vegetative', 11), height: 42 }
    const frames = normalizeFrames([frame(3, 'Mature', 30), first, replacement, frame(2, 'Vegetative', 20)])
    expect(frames.map(item => item.age)).toEqual([1, 2, 3])
    expect(frames[0]).toMatchObject({ step: 11, height: 42, lifeStage: 'Vegetative' })
    expect(appendFrame(frames, frame(2, 'Vegetative', 21))[1].step).toBe(21)
    expect(appendFrame(frames, frame(0.5, 'Seedling', 5)).map(item => item.age)).toEqual([0.5, 1, 2, 3])
  })

  it('parses valid backend frames and rejects malformed age data', () => {
    const parsed = toPoint({
      step: 7,
      age: 1.5,
      lifeStage: 'Vegetative',
      metrics: { height: 2, totalBranchLength: 3, branchCount: 4, leafCount: 5, canopyWidth: 6 },
      plantState: { schema: 'plantsim.skeleton', skeleton: { nodes: [] } },
    })
    expect(parsed).toMatchObject({ step: 7, age: 1.5, height: 2, leafCount: 5 })
    expect(parsed?.plantState?.schema).toBe('plantsim.skeleton')
    expect(toPoint({ age: 'not-a-number' })).toBeNull()
  })

  it('samples charts while preserving first and last frames and shares the same plot scale', () => {
    const frames = Array.from({ length: 11 }, (_, index) => frame(index, 'Mature', index))
    const sampled = sample(frames, 4)
    expect(sampled).toHaveLength(4)
    expect(sampled[0]).toBe(frames[0])
    expect(sampled.at(-1)).toBe(frames.at(-1))
    const path = makePath([frame(0, 'Seedling', 0), frame(10, 'Mature', 10)], 'height', 10, 20)
    expect(path).toBe('M 0.00 218.10 L 1000.00 119.10')
  })
})
