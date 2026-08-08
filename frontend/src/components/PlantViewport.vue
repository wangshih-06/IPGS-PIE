<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from 'vue'

type PlantType = 'cherry' | 'willow' | 'pine' | 'passion'
type InteractionMode = 'select' | 'orbit' | 'wind'
type Point3 = { x: number; y: number; z: number }
type Branch = { points: Point3[]; width: number; color: string }
type Leaf = { center: Point3; size: number; angle: number; color: string; kind: 'leaf' | 'blossom' | 'needle' | 'passion-leaf' | 'passion-flower' | 'fruit' }
type PlantScene = { label: string; branches: Branch[]; leaves: Leaf[]; accent: string }
type SnapshotNode = { id: number; parentId: number; position: [number, number, number]; radius?: number; active?: boolean; type?: string }
type SnapshotLeaf = { id: number; parentNodeId: number; position: [number, number, number]; size?: [number, number]; active?: boolean }
type PlantSnapshot = { schema?: string; plant?: { name?: string; species?: string; lifeStage?: string }; skeleton?: { nodes?: SnapshotNode[]; leaves?: SnapshotLeaf[] } }

const props = withDefaults(defineProps<{
  lightIntensity: number
  windIntensity: number
  playing: boolean
  plantType: PlantType
  interactionMode: InteractionMode
  resetToken: number
  snapshot?: PlantSnapshot
  growthProgress?: number
}>(), {
  lightIntensity: 0.8,
  windIntensity: 0.28,
  playing: true,
  plantType: 'cherry',
  interactionMode: 'orbit',
  resetToken: 0,
  snapshot: undefined,
  growthProgress: 1,
})

const canvasRef = ref<HTMLCanvasElement | null>(null)
const dragging = ref(false)
const view = reactive({ yaw: 0, pitch: 0, zoom: 1, panX: 0, panY: 0 })
let dragState: { x: number; y: number; mode: 'orbit' | 'pan' } | null = null
let animationFrame = 0
let observer: ResizeObserver | null = null
let startedAt = 0
let documentVisible = true

function point(x: number, y: number, z = 0): Point3 {
  return { x, y, z }
}

function addBlossomCluster(target: Leaf[], center: Point3, size: number, phase: number) {
  const offsets = [
    [0, 0, 0],
    [0.11, 0.05, 0.03],
    [-0.1, 0.07, -0.03],
    [0.03, -0.11, 0.04],
  ]
  offsets.forEach(([x, y, z], index) => {
    target.push({
      center: point(center.x + x, center.y + y, center.z + z),
      size: size * (index === 0 ? 1.05 : 0.7),
      angle: phase + index * 0.8,
      color: index % 3 === 0 ? '#e89aa5' : index % 2 ? '#efb5a9' : '#f3c3b5',
      kind: 'blossom',
    })
  })
}

function addWillowCurtain(target: Leaf[], path: Point3[], phase: number, color: string) {
  for (let segment = 1; segment < path.length; segment += 1) {
    const from = path[segment - 1]
    const to = path[segment]
    const dx = to.x - from.x
    const dz = to.z - from.z
    const length = Math.max(0.001, Math.hypot(dx, dz))
    const normalX = -dz / length
    const normalZ = dx / length
    for (let i = 0; i < 7; i += 1) {
      const t = (i + 1) / 8
      const base = point(from.x + dx * t, from.y + (to.y - from.y) * t, from.z + dz * t)
      const side = Math.sin(phase + segment * 1.7 + i * 1.9) * 0.15
      const droop = (segment - 1) * 0.04 + i * 0.026
      target.push({
        center: point(base.x + normalX * side, base.y - droop, base.z + normalZ * side),
        size: 0.11 + (1 - t) * 0.045,
        angle: Math.atan2(to.y - from.y, length) + Math.PI * 0.5 + side,
        color: (i + segment) % 4 === 0 ? '#bfd986' : color,
        kind: 'leaf',
      })
    }
  }
}

function addNeedleCluster(target: Leaf[], center: Point3, radius: number, phase: number, color: string) {
  for (let i = 0; i < 12; i += 1) {
    const angle = phase + (i / 12) * Math.PI * 2
    target.push({
      center: point(center.x + Math.cos(angle) * radius * 0.24, center.y + Math.sin(i * 1.7) * radius * 0.1, center.z + Math.sin(angle) * radius * 0.24),
      size: radius * (0.72 + (i % 3) * 0.08),
      angle,
      color: i % 4 === 0 ? '#9dcc72' : color,
      kind: 'needle',
    })
  }
}

function buildCherry(): PlantScene {
  const branches: Branch[] = [
    { points: [point(0, 0), point(0.18, 0.66), point(-0.04, 1.42), point(-0.24, 2.16), point(0.06, 2.78), point(-0.12, 3.45), point(0.08, 4.18)], width: 0.31, color: '#8a573b' },
    { points: [point(0, 0.1), point(-0.62, 0.05, 0.08), point(-1.4, 0.02, 0.16), point(-1.92, 0.12, 0.08)], width: 0.12, color: '#97613f' },
    { points: [point(0, 0.08), point(0.55, 0.02, -0.12), point(1.3, 0.06, -0.18), point(1.84, 0.2, -0.1)], width: 0.105, color: '#97613f' },
    { points: [point(-0.2, 1.42), point(-0.96, 1.96, 0.2), point(-1.9, 2.34, 0.05), point(-2.72, 2.65, 0.12)], width: 0.17, color: '#a36843' },
    { points: [point(-0.1, 1.78), point(-0.88, 2.48, -0.32), point(-1.72, 2.98, -0.16), point(-2.34, 3.32, -0.1)], width: 0.13, color: '#a96d46' },
    { points: [point(0, 1.78), point(0.94, 2.32, -0.22), point(1.92, 2.48, -0.08), point(2.92, 2.68, -0.04)], width: 0.17, color: '#a36843' },
    { points: [point(0.02, 2.2), point(0.9, 2.94, 0.3), point(1.78, 3.38, 0.18), point(2.45, 3.76, 0.12)], width: 0.13, color: '#a96d46' },
    { points: [point(-0.12, 2.3), point(-0.72, 3.04, 0.54), point(-1.5, 3.55, 0.46), point(-2.12, 3.9, 0.34)], width: 0.11, color: '#b1744b' },
    { points: [point(0, 2.68), point(0.58, 3.22, -0.52), point(1.22, 3.72, -0.44), point(1.88, 4.1, -0.3)], width: 0.11, color: '#b1744b' },
    { points: [point(-0.1, 2.72), point(-0.46, 3.32, -0.24), point(-0.82, 3.92, -0.2), point(-1.08, 4.34, -0.12)], width: 0.08, color: '#b77a50' },
    { points: [point(0.02, 2.92), point(0.38, 3.5, 0.34), point(0.62, 4.06, 0.28), point(0.72, 4.42, 0.2)], width: 0.08, color: '#b77a50' },
    { points: [point(-1.9, 2.34, 0.05), point(-2.32, 2.9, 0.16), point(-2.98, 3.12, 0.12)], width: 0.07, color: '#bc7d50' },
    { points: [point(1.92, 2.48, -0.08), point(2.36, 2.92, -0.02), point(3.12, 3.05, 0.04)], width: 0.07, color: '#bc7d50' },
    { points: [point(-1.72, 2.98, -0.16), point(-2.0, 3.46, -0.06), point(-2.52, 3.72, -0.02)], width: 0.06, color: '#c18353' },
    { points: [point(1.78, 3.38, 0.18), point(2.18, 3.76, 0.08), point(2.76, 4.0, 0.02)], width: 0.06, color: '#c18353' },
  ]
  const blossomPoints: Array<[number, number, number, number]> = [
    [-2.72, 2.66, 0.12, 0.18], [-2.98, 3.12, 0.12, 0.19], [-2.52, 3.72, -0.02, 0.18], [-2.34, 3.32, -0.1, 0.2],
    [-2.12, 3.92, 0.34, 0.19], [-1.9, 2.36, 0.05, 0.22], [-1.72, 2.98, -0.16, 0.2], [-1.5, 3.56, 0.46, 0.19],
    [-1.08, 4.34, -0.12, 0.21], [-0.82, 3.94, -0.2, 0.2], [-0.7, 3.05, 0.54, 0.18], [-0.46, 3.32, -0.24, 0.17],
    [2.92, 2.7, -0.04, 0.18], [3.12, 3.08, 0.04, 0.19], [2.76, 4.02, 0.02, 0.18], [2.45, 3.78, 0.12, 0.2],
    [2.18, 3.78, 0.08, 0.19], [1.92, 2.5, -0.08, 0.22], [1.78, 3.4, 0.18, 0.2], [1.22, 3.74, -0.44, 0.19],
    [0.72, 4.42, 0.2, 0.21], [0.62, 4.08, 0.28, 0.2], [0.58, 3.24, -0.52, 0.18], [0.38, 3.5, 0.34, 0.17],
    [0.08, 4.18, 0, 0.24], [0, 3.58, 0.18, 0.2], [-0.06, 3.12, -0.42, 0.18], [0.08, 2.86, 0.52, 0.17],
  ]
  const leaves: Leaf[] = []
  blossomPoints.forEach(([x, y, z, size], index) => addBlossomCluster(leaves, point(x, y, z), size, index * 0.57))
  ;[
    [-1.72, 2.34, 0.14], [-0.72, 2.84, -0.38], [0.74, 2.94, 0.34], [1.7, 2.52, -0.16],
    [-0.4, 3.42, 0.28], [0.42, 3.5, -0.3],
  ].forEach(([x, y, z], index) => leaves.push({
    center: point(x, y, z), size: 0.13, angle: index * 0.9, color: '#9dca76', kind: 'leaf',
  }))
  return { label: 'SPRING CHERRY / BLOOM', branches, leaves, accent: '#efb5a9' }
}

function buildWillow(): PlantScene {
  const branches: Branch[] = [
    { points: [point(0, 0), point(0.12, 0.8), point(-0.05, 1.7), point(0.08, 2.65), point(-0.02, 3.65)], width: 0.29, color: '#805b43' },
    { points: [point(0, 0.08), point(-0.7, 0.05, 0.1), point(-1.45, 0.14, 0.18)], width: 0.12, color: '#956b4a' },
    { points: [point(0, 0.08), point(0.62, 0.02, -0.12), point(1.4, 0.16, -0.2)], width: 0.11, color: '#956b4a' },
    { points: [point(0, 0.12), point(-0.42, -0.02, 0.35), point(-0.92, 0.08, 0.58), point(-1.3, 0.2, 0.72)], width: 0.08, color: '#a27450' },
    { points: [point(0, 0.12), point(0.42, -0.02, -0.34), point(0.94, 0.08, -0.56), point(1.32, 0.2, -0.68)], width: 0.075, color: '#a27450' },
  ]
  const leaves: Leaf[] = []
  const curtains: Array<{ path: Point3[]; width: number; phase: number }> = [
    { path: [point(-0.02, 1.08), point(-0.72, 1.92, 0.22), point(-1.48, 1.52, 0.12), point(-1.82, 0.36, 0.08)], width: 0.15, phase: 0.1 },
    { path: [point(0.02, 1.3), point(0.82, 2.08, -0.26), point(1.56, 1.52, -0.12), point(1.9, 0.3, -0.05)], width: 0.15, phase: 1.3 },
    { path: [point(-0.02, 1.62), point(-0.96, 2.42, -0.38), point(-1.62, 1.86, -0.22), point(-1.68, 0.72, -0.14)], width: 0.12, phase: 2.1 },
    { path: [point(0.04, 1.78), point(0.96, 2.66, 0.36), point(1.62, 2.02, 0.2), point(1.7, 0.66, 0.15)], width: 0.12, phase: 3.2 },
    { path: [point(-0.04, 2.04), point(-0.58, 2.92, 0.46), point(-0.72, 1.3, 0.4)], width: 0.095, phase: 0.8 },
    { path: [point(0.04, 2.22), point(0.58, 3.12, -0.48), point(0.72, 1.26, -0.4)], width: 0.095, phase: 2.8 },
    { path: [point(-0.02, 2.52), point(-0.24, 3.4, -0.12), point(-0.32, 1.22, -0.14)], width: 0.08, phase: 4.1 },
    { path: [point(0.02, 2.7), point(0.26, 3.5, 0.1), point(0.34, 1.3, 0.14)], width: 0.08, phase: 5.2 },
    { path: [point(-0.05, 1.42, 0.56), point(-0.7, 2.28, 0.7), point(-1.32, 1.72, 0.66), point(-1.48, 0.42, 0.6)], width: 0.085, phase: 0.45 },
    { path: [point(0.04, 1.48, -0.56), point(0.72, 2.34, -0.7), point(1.34, 1.76, -0.64), point(1.5, 0.38, -0.58)], width: 0.08, phase: 1.95 },
    { path: [point(-0.04, 2.02, 0.62), point(-0.42, 2.9, 0.78), point(-0.62, 2.0, 0.76), point(-0.72, 0.62, 0.7)], width: 0.065, phase: 3.35 },
    { path: [point(0.04, 2.18, -0.62), point(0.46, 3.08, -0.78), point(0.66, 2.04, -0.72), point(0.76, 0.58, -0.66)], width: 0.06, phase: 4.65 },
  ]
  curtains.forEach((curtain) => {
    branches.push({ points: curtain.path, width: curtain.width, color: '#a9784f' })
    addWillowCurtain(leaves, curtain.path, curtain.phase, curtain.phase % 2 > 1 ? '#a5c878' : '#8fba78')
    const last = curtain.path[curtain.path.length - 1]
    branches.push({
      points: [curtain.path[2], point(last.x + 0.28, last.y + 0.22, last.z + 0.08), point(last.x + 0.38, last.y - 0.12, last.z + 0.1)],
      width: Math.max(0.038, curtain.width * 0.42),
      color: '#b27b52',
    })
    addWillowCurtain(leaves, [curtain.path[2], point(last.x + 0.28, last.y + 0.22, last.z + 0.08), point(last.x + 0.38, last.y - 0.12, last.z + 0.1)], curtain.phase + 0.7, '#9fc57b')
  })
  return { label: 'WEEPING WILLOW', branches, leaves, accent: '#b4d17c' }
}

function buildPine(): PlantScene {
  const branches: Branch[] = [{ points: [point(0, 0), point(0, 1.5), point(0, 3), point(0, 4.55)], width: 0.25, color: '#725138' }]
  const leaves: Leaf[] = []
  const tiers = [
    [1.02, 1.65], [1.42, 1.92], [1.82, 2.12], [2.2, 1.96], [2.58, 1.75], [2.92, 1.55],
    [3.25, 1.32], [3.55, 1.08], [3.82, 0.8], [4.06, 0.55],
  ]
  tiers.forEach(([y, length], tierIndex) => {
    const branchCount = tierIndex < 3 ? 7 : 6
    for (let i = 0; i < branchCount; i += 1) {
      const angle = (i / branchCount) * Math.PI * 2 + tierIndex * 0.31
      const end = point(Math.cos(angle) * length, y - 0.06 + Math.sin(i * 1.4) * 0.04, Math.sin(angle) * length)
      const mid = point(end.x * 0.52, y + 0.12, end.z * 0.52)
      branches.push({ points: [point(0, y), mid, end], width: Math.max(0.045, 0.11 - tierIndex * 0.006), color: tierIndex % 2 ? '#806040' : '#936943' })
      const twig = point(end.x * 1.08, end.y + 0.08, end.z * 1.08)
      branches.push({ points: [end, twig], width: Math.max(0.025, 0.055 - tierIndex * 0.003), color: '#a27549' })
      addNeedleCluster(leaves, end, 0.25 - tierIndex * 0.009, angle + tierIndex * 0.2, tierIndex % 2 ? '#5f9b68' : '#77b879')
    }
  })
  addNeedleCluster(leaves, point(0, 4.5, 0), 0.3, 0, '#86bd78')
  return { label: 'BLACK PINE', branches, leaves, accent: '#77b879' }
}

function buildPassionFruit(): PlantScene {
  const branches: Branch[] = [
    { points: [point(0, 0), point(0.16, 0.72), point(-0.08, 1.48), point(0.18, 2.25), point(-0.02, 3.1), point(0.22, 3.9), point(0.1, 4.46)], width: 0.15, color: '#6e5a3d' },
    { points: [point(-0.02, 0.7), point(-0.52, 0.94, 0.12), point(-1.18, 1.08, 0.18), point(-1.88, 1.36, 0.1), point(-2.42, 1.7, 0.06)], width: 0.095, color: '#73975d' },
    { points: [point(0.12, 0.9), point(0.72, 1.12, -0.18), point(1.38, 1.38, -0.1), point(2.02, 1.76, -0.02), point(2.48, 2.22, 0.04)], width: 0.095, color: '#71945b' },
    { points: [point(0.04, 1.46), point(-0.68, 1.62, 0.24), point(-1.36, 1.9, 0.18), point(-2.04, 2.28, 0.1), point(-2.48, 2.82, 0.04)], width: 0.088, color: '#6e9b5b' },
    { points: [point(0.1, 1.62), point(0.8, 1.9, -0.3), point(1.5, 2.22, -0.2), point(2.18, 2.64, -0.1), point(2.62, 3.18, -0.02)], width: 0.087, color: '#6e9b5b' },
    { points: [point(-0.02, 2.0), point(-0.72, 2.46, 0.34), point(-1.46, 2.84, 0.24), point(-2.12, 3.32, 0.14), point(-2.3, 3.78, 0.08)], width: 0.076, color: '#6d9958' },
    { points: [point(0.12, 2.28), point(0.78, 2.62, -0.38), point(1.46, 3.02, -0.28), point(2.12, 3.5, -0.16), point(2.32, 3.96, -0.08)], width: 0.076, color: '#6d9958' },
    { points: [point(0.02, 2.82), point(-0.44, 3.2, 0.48), point(-0.88, 3.72, 0.38), point(-0.82, 4.2, 0.26)], width: 0.062, color: '#78a764' },
    { points: [point(0.16, 3.04), point(0.58, 3.38, -0.34), point(0.92, 3.86, -0.25), point(0.8, 4.28, -0.16)], width: 0.058, color: '#78a764' },
  ]
  const tendrils: Point3[][] = [
    [point(-0.52, 0.94, 0.12), point(-0.8, 1.18, 0.18), point(-0.65, 1.42, 0.24), point(-0.38, 1.34, 0.2), point(-0.3, 1.12, 0.16)],
    [point(0.72, 1.12, -0.18), point(1.0, 1.36, -0.24), point(0.86, 1.6, -0.3), point(0.58, 1.54, -0.26), point(0.54, 1.3, -0.2)],
    [point(-0.68, 1.62, 0.24), point(-0.94, 1.88, 0.3), point(-0.8, 2.12, 0.34), point(-0.52, 2.06, 0.3), point(-0.46, 1.82, 0.26)],
    [point(0.8, 1.9, -0.3), point(1.08, 2.16, -0.36), point(0.92, 2.4, -0.4), point(0.64, 2.34, -0.36), point(0.58, 2.1, -0.32)],
    [point(-0.72, 2.46, 0.34), point(-1.0, 2.74, 0.4), point(-0.84, 2.98, 0.44), point(-0.56, 2.9, 0.4), point(-0.5, 2.66, 0.36)],
    [point(0.78, 2.62, -0.38), point(1.08, 2.88, -0.44), point(0.92, 3.14, -0.48), point(0.64, 3.08, -0.44), point(0.58, 2.82, -0.4)],
    [point(-0.44, 3.2, 0.48), point(-0.68, 3.46, 0.54), point(-0.56, 3.72, 0.58), point(-0.3, 3.66, 0.54), point(-0.25, 3.42, 0.5)],
    [point(0.58, 3.38, -0.34), point(0.86, 3.62, -0.4), point(0.74, 3.88, -0.44), point(0.46, 3.82, -0.4), point(0.4, 3.58, -0.36)],
    [point(-1.88, 1.36, 0.1), point(-2.12, 1.58, 0.16), point(-2.02, 1.8, 0.2), point(-1.78, 1.74, 0.16), point(-1.72, 1.52, 0.12)],
    [point(2.02, 1.76, -0.02), point(2.26, 1.98, 0.04), point(2.18, 2.2, 0.08), point(1.94, 2.14, 0.04), point(1.88, 1.92, 0)],
  ]
  tendrils.forEach((path) => branches.push({ points: path, width: 0.026, color: '#82b866' }))

  const leaves: Leaf[] = []
  const leafPoints: Array<[number, number, number, number, number]> = [
    [-0.5, 1.02, 0.14, 0.26, -0.7], [-1.1, 1.18, 0.2, 0.28, 0.3], [-1.74, 1.4, 0.12, 0.26, -0.4], [-2.26, 1.74, 0.08, 0.24, 0.2],
    [0.62, 1.16, -0.22, 0.27, 0.45], [1.22, 1.4, -0.16, 0.3, -0.25], [1.82, 1.76, -0.06, 0.27, 0.35], [2.3, 2.18, 0.02, 0.25, -0.2],
    [-0.72, 1.66, 0.26, 0.29, -0.5], [-1.34, 1.94, 0.22, 0.3, 0.4], [-1.96, 2.3, 0.12, 0.27, -0.15], [-2.34, 2.76, 0.06, 0.25, 0.3],
    [0.76, 1.94, -0.32, 0.28, 0.55], [1.38, 2.24, -0.24, 0.31, -0.35], [1.98, 2.66, -0.14, 0.28, 0.25], [2.48, 3.12, -0.04, 0.24, -0.3],
    [-0.74, 2.48, 0.36, 0.29, -0.3], [-1.38, 2.86, 0.28, 0.31, 0.45], [-1.98, 3.34, 0.16, 0.27, -0.2], [-2.2, 3.72, 0.1, 0.23, 0.35],
    [0.8, 2.64, -0.4, 0.29, 0.45], [1.4, 3.04, -0.3, 0.3, -0.35], [1.98, 3.52, -0.18, 0.27, 0.2], [2.28, 3.9, -0.08, 0.23, -0.25],
    [-0.42, 3.22, 0.5, 0.27, 0.2], [-0.82, 3.74, 0.42, 0.25, -0.45], [-0.76, 4.16, 0.3, 0.22, 0.15],
    [0.56, 3.4, -0.36, 0.27, 0.55], [0.9, 3.86, -0.26, 0.25, -0.35], [0.78, 4.24, -0.18, 0.21, 0.2],
    [-0.18, 1.48, -0.3, 0.24, 0.9], [0.18, 2.18, 0.34, 0.25, -0.8], [-0.08, 2.86, -0.46, 0.26, 0.7], [0.22, 3.58, 0.18, 0.24, -0.5],
  ]
  leafPoints.forEach(([x, y, z, size, angle], index) => leaves.push({
    center: point(x, y, z), size, angle, color: index % 2 ? '#5f9d5f' : '#79b76a', kind: 'passion-leaf',
  }))
  const flowerPoints: Array<[number, number, number, number]> = [
    [-1.42, 1.66, 0.24, 0.24], [1.18, 1.98, -0.2, 0.23], [-1.18, 2.38, 0.26, 0.25], [1.38, 2.7, -0.24, 0.24],
    [-1.34, 3.02, 0.2, 0.25], [1.42, 3.48, -0.2, 0.24], [-0.04, 3.7, 0.14, 0.26], [-0.8, 4.0, 0.34, 0.22], [0.82, 4.02, -0.24, 0.22],
  ]
  flowerPoints.forEach(([x, y, z, size], index) => leaves.push({ center: point(x, y, z), size, angle: index * 0.8, color: '#a992d1', kind: 'passion-flower' }))
  const fruitPoints: Array<[number, number, number, number]> = [
    [-1.72, 1.58, 0.12, 0.22], [1.74, 1.98, -0.1, 0.24], [-1.58, 2.76, 0.16, 0.2], [1.78, 2.98, -0.14, 0.22],
    [-1.86, 3.5, 0.1, 0.21], [1.94, 3.72, -0.08, 0.22], [0.12, 4.08, 0.16, 0.2],
  ]
  fruitPoints.forEach(([x, y, z, size], index) => leaves.push({ center: point(x, y, z), size, angle: index * 0.4, color: index % 2 ? '#b7c767' : '#d7bd69', kind: 'fruit' }))
  return { label: 'PASSION FRUIT VINE', branches, leaves, accent: '#a992d1' }
}

function snapshotPoint(value: [number, number, number] | undefined): Point3 | null {
  if (!value || value.length !== 3 || !value.every(Number.isFinite)) return null
  return point(value[0], value[1], value[2])
}

function buildRecordedScene(snapshot: PlantSnapshot | undefined): PlantScene | null {
  const skeleton = snapshot?.skeleton
  if (snapshot?.schema !== 'plantsim.skeleton' || !Array.isArray(skeleton?.nodes) || !skeleton.nodes.length) return null
  const nodes = skeleton.nodes.filter(node => node.active !== false)
  const byId = new Map(nodes.map(node => [node.id, node]))
  const branches: Branch[] = []
  for (const node of nodes) {
    const parent = byId.get(node.parentId)
    const from = snapshotPoint(parent?.position)
    const to = snapshotPoint(node.position)
    if (!parent || !from || !to) continue
    const isRoot = node.type === 'root'
    branches.push({
      points: [from, to],
      width: Math.max(0.028, Math.min(0.34, (Number.isFinite(node.radius) ? node.radius! : 0.03) * 2.1)),
      color: isRoot ? '#725342' : node.type === 'stem' ? '#91603f' : '#ad7149',
    })
  }
  const leaves: Leaf[] = []
  for (const leaf of skeleton.leaves ?? []) {
    const center = leaf.active === false ? null : snapshotPoint(leaf.position)
    if (!center) continue
    const dimensions = leaf.size ?? [0.12, 0.06]
    const size = Math.max(0.05, Math.min(0.28, Math.max(dimensions[0] || 0, dimensions[1] || 0)))
    leaves.push({ center, size, angle: Math.atan2(center.z, center.x), color: '#78b96e', kind: 'leaf' })
  }
  if (!branches.length && !leaves.length) return null
  const identity = snapshot.plant?.name || snapshot.plant?.species || 'SIMULATION SKELETON'
  return { label: `${identity.toUpperCase()} / ${nodes.length} NODES`, branches, leaves, accent: '#86db9b' }
}

function scaleFallbackScene(source: PlantScene, progress: number): PlantScene {
  const t = Math.max(0.035, Math.min(1, progress))
  const horizontal = 0.25 + t * 0.75
  const visibleLeaves = Math.max(0, Math.ceil(source.leaves.length * Math.max(0, (t - 0.12) / 0.88)))
  return {
    label: `${source.label} / OFFLINE GROWTH`,
    accent: source.accent,
    branches: source.branches.map(branch => ({
      ...branch,
      width: Math.max(0.016, branch.width * (0.28 + t * 0.72)),
      points: branch.points.map(item => point(item.x * horizontal, item.y * t, item.z * horizontal)),
    })),
    leaves: source.leaves.slice(0, visibleLeaves).map(leaf => ({
      ...leaf,
      size: leaf.size * (0.25 + t * 0.75),
      center: point(leaf.center.x * horizontal, leaf.center.y * t, leaf.center.z * horizontal),
    })),
  }
}

const fallbackScene = computed(() => {
  if (props.plantType === 'willow') return buildWillow()
  if (props.plantType === 'pine') return buildPine()
  if (props.plantType === 'passion') return buildPassionFruit()
  return buildCherry()
})
const scene = computed(() => buildRecordedScene(props.snapshot) ?? scaleFallbackScene(fallbackScene.value, props.growthProgress))
const orderedBranches = computed(() => [...scene.value.branches].sort((a, b) => {
  const aDepth = a.points.reduce((sum, item) => sum + item.z, 0)
  const bDepth = b.points.reduce((sum, item) => sum + item.z, 0)
  return bDepth - aDepth
}))
const orderedLeaves = computed(() => [...scene.value.leaves].sort((a, b) => b.center.z - a.center.z))

function resizeCanvas(canvas: HTMLCanvasElement) {
  const rect = canvas.getBoundingClientRect()
  const dpr = Math.min(window.devicePixelRatio || 1, 2)
  const width = Math.max(1, Math.floor(rect.width * dpr))
  const height = Math.max(1, Math.floor(rect.height * dpr))
  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width
    canvas.height = height
  }
}

function project(point3: Point3, width: number, height: number, scale: number, elapsed: number) {
  const sway = props.playing || props.interactionMode === 'wind' ? Math.sin(elapsed * 1.35 + point3.z * 2.2) * props.windIntensity * 0.085 * (point3.y / 4.5) : 0
  const x = point3.x + sway
  const cosYaw = Math.cos(view.yaw)
  const sinYaw = Math.sin(view.yaw)
  const rotatedX = x * cosYaw + point3.z * sinYaw
  const rotatedZ = -x * sinYaw + point3.z * cosYaw
  const cosPitch = Math.cos(view.pitch)
  const sinPitch = Math.sin(view.pitch)
  const rotatedY = point3.y * cosPitch - rotatedZ * sinPitch
  const depth = point3.y * sinPitch + rotatedZ * cosPitch
  const perspective = 1 / (1 + depth * 0.065)
  return {
    x: width * 0.5 + view.panX + rotatedX * scale * view.zoom * perspective,
    y: height * 0.73 + view.panY - rotatedY * scale * view.zoom * perspective,
    depth,
    perspective,
  }
}

function drawBranch(ctx: CanvasRenderingContext2D, branch: Branch, width: number, height: number, scale: number, elapsed: number) {
  const projected = branch.points.map((item) => project(item, width, height, scale, elapsed))
  ctx.beginPath()
  projected.forEach((item, index) => index === 0 ? ctx.moveTo(item.x, item.y) : ctx.lineTo(item.x, item.y))
  const averageDepth = projected.reduce((sum, item) => sum + item.depth, 0) / projected.length
  const strokeWidth = Math.max(1.2, branch.width * scale * view.zoom * (1 / (1 + averageDepth * 0.04)))
  ctx.lineWidth = strokeWidth + Math.max(1.5, strokeWidth * 0.24)
  ctx.strokeStyle = 'rgba(45, 28, 23, .42)'
  ctx.stroke()
  ctx.lineWidth = strokeWidth
  ctx.strokeStyle = branch.color
  ctx.lineCap = 'round'
  ctx.lineJoin = 'round'
  ctx.stroke()
  ctx.lineWidth = Math.max(0.7, strokeWidth * 0.16)
  ctx.strokeStyle = 'rgba(241, 177, 119, .24)'
  ctx.setLineDash([Math.max(2, strokeWidth * 0.55), Math.max(5, strokeWidth * 2.2)])
  ctx.stroke()
  ctx.setLineDash([])
}

function drawLeaf(ctx: CanvasRenderingContext2D, leaf: Leaf, width: number, height: number, scale: number, elapsed: number) {
  const item = project(leaf.center, width, height, scale, elapsed)
  const size = leaf.size * scale * view.zoom * item.perspective
  ctx.save()
  ctx.translate(item.x, item.y)
  ctx.rotate(leaf.angle + view.yaw * 0.14)
  if (leaf.kind === 'blossom') {
    ctx.shadowColor = 'rgba(244, 170, 178, .28)'
    ctx.shadowBlur = Math.max(2, size * 0.16)
    for (let i = 0; i < 5; i += 1) {
      const angle = (i / 5) * Math.PI * 2
      ctx.beginPath()
      ctx.ellipse(Math.cos(angle) * size * 0.42, Math.sin(angle) * size * 0.42, size * 0.42, size * 0.26, angle, 0, Math.PI * 2)
      ctx.fillStyle = leaf.color
      ctx.fill()
    }
    ctx.shadowBlur = 0
    ctx.beginPath()
    ctx.arc(0, 0, Math.max(1.5, size * 0.15), 0, Math.PI * 2)
    ctx.fillStyle = '#f3d37b'
    ctx.fill()
    ctx.fillStyle = 'rgba(255, 246, 205, .76)'
    for (let i = 0; i < 3; i += 1) {
      ctx.beginPath()
      ctx.arc(Math.cos(i * 2.1) * size * 0.27, Math.sin(i * 2.1) * size * 0.27, Math.max(0.7, size * 0.035), 0, Math.PI * 2)
      ctx.fill()
    }
  } else if (leaf.kind === 'passion-flower') {
    ctx.shadowColor = 'rgba(176, 144, 222, .32)'
    ctx.shadowBlur = Math.max(2, size * 0.18)
    for (let i = 0; i < 5; i += 1) {
      const angle = (i / 5) * Math.PI * 2
      ctx.beginPath()
      ctx.ellipse(Math.cos(angle) * size * 0.38, Math.sin(angle) * size * 0.38, size * 0.46, size * 0.22, angle, 0, Math.PI * 2)
      ctx.fillStyle = i % 2 ? '#f2e9f2' : '#d3b0e5'
      ctx.fill()
    }
    ctx.shadowBlur = 0
    // 百香果花的冠丝是最容易辨认的结构：由外向内形成紫白色放射纹。
    ctx.strokeStyle = '#8b65b1'
    ctx.lineWidth = Math.max(0.8, size * 0.052)
    for (let i = 0; i < 18; i += 1) {
      const angle = (i / 18) * Math.PI * 2
      ctx.beginPath()
      ctx.moveTo(Math.cos(angle) * size * 0.12, Math.sin(angle) * size * 0.12)
      ctx.lineTo(Math.cos(angle) * size * 0.76, Math.sin(angle) * size * 0.76)
      ctx.stroke()
    }
    ctx.strokeStyle = 'rgba(255, 249, 255, .85)'
    ctx.lineWidth = Math.max(0.55, size * 0.028)
    for (let i = 0; i < 12; i += 1) {
      const angle = (i / 12) * Math.PI * 2 + 0.13
      ctx.beginPath()
      ctx.moveTo(Math.cos(angle) * size * 0.2, Math.sin(angle) * size * 0.2)
      ctx.lineTo(Math.cos(angle) * size * 0.62, Math.sin(angle) * size * 0.62)
      ctx.stroke()
    }
    ctx.beginPath()
    ctx.arc(0, 0, Math.max(1.5, size * 0.17), 0, Math.PI * 2)
    ctx.fillStyle = '#f4d27c'
    ctx.fill()
    ctx.beginPath()
    ctx.arc(0, -size * 0.31, Math.max(1, size * 0.09), 0, Math.PI * 2)
    ctx.fillStyle = '#f5f1d3'
    ctx.fill()
    ctx.strokeStyle = '#7e5b9b'
    ctx.lineWidth = Math.max(0.8, size * 0.04)
    ctx.beginPath()
    ctx.moveTo(0, -size * 0.12)
    ctx.lineTo(0, -size * 0.42)
    ctx.moveTo(-size * 0.12, -size * 0.08)
    ctx.lineTo(-size * 0.28, -size * 0.32)
    ctx.moveTo(size * 0.12, -size * 0.08)
    ctx.lineTo(size * 0.28, -size * 0.32)
    ctx.stroke()
  } else if (leaf.kind === 'fruit') {
    const fruitGradient = ctx.createRadialGradient(-size * 0.24, -size * 0.28, size * 0.05, 0, 0, size * 0.75)
    fruitGradient.addColorStop(0, '#f0e18d')
    fruitGradient.addColorStop(0.4, leaf.color)
    fruitGradient.addColorStop(1, 'rgba(79, 103, 54, .9)')
    ctx.beginPath()
    ctx.ellipse(0, 0, size * 0.66, size * 0.82, -0.12, 0, Math.PI * 2)
    ctx.fillStyle = fruitGradient
    ctx.fill()
    ctx.strokeStyle = 'rgba(237, 233, 151, .42)'
    ctx.lineWidth = Math.max(0.8, size * 0.045)
    ctx.stroke()
    ctx.beginPath()
    ctx.arc(-size * 0.25, -size * 0.32, Math.max(1, size * 0.12), 0, Math.PI * 2)
    ctx.fillStyle = 'rgba(255, 248, 205, .7)'
    ctx.fill()
    // 果皮上的细小斑点和顶部果梗，避免果实看起来像简单圆点。
    ctx.fillStyle = 'rgba(93, 113, 57, .34)'
    for (let i = 0; i < 9; i += 1) {
      const spotAngle = i * 2.37 + leaf.angle
      const spotRadius = size * (0.18 + (i % 3) * 0.13)
      ctx.beginPath()
      ctx.arc(Math.cos(spotAngle) * spotRadius * 0.62, Math.sin(spotAngle) * spotRadius, Math.max(0.45, size * 0.025), 0, Math.PI * 2)
      ctx.fill()
    }
    ctx.strokeStyle = '#647b4b'
    ctx.lineWidth = Math.max(0.7, size * 0.045)
    ctx.beginPath()
    ctx.moveTo(0, -size * 0.72)
    ctx.lineTo(size * 0.04, -size * 0.93)
    ctx.stroke()
    ctx.fillStyle = '#66844e'
    ctx.beginPath()
    ctx.ellipse(-size * 0.11, -size * 0.76, size * 0.16, size * 0.06, -0.4, 0, Math.PI * 2)
    ctx.ellipse(size * 0.11, -size * 0.76, size * 0.16, size * 0.06, 0.4, 0, Math.PI * 2)
    ctx.fill()
  } else if (leaf.kind === 'passion-leaf') {
    const leafGradient = ctx.createLinearGradient(0, -size * 0.8, 0, size * 0.8)
    leafGradient.addColorStop(0, '#9bc86e')
    leafGradient.addColorStop(0.55, leaf.color)
    leafGradient.addColorStop(1, '#3f804e')
    ctx.beginPath()
    ctx.moveTo(0, size * 0.78)
    ctx.quadraticCurveTo(-size * 0.16, size * 0.34, -size * 0.18, size * 0.04)
    ctx.quadraticCurveTo(-size * 0.7, size * 0.08, -size * 0.66, -size * 0.42)
    ctx.quadraticCurveTo(-size * 0.62, -size * 0.64, -size * 0.28, -size * 0.57)
    ctx.quadraticCurveTo(-size * 0.1, -size * 0.52, 0, -size * 0.2)
    ctx.quadraticCurveTo(size * 0.1, -size * 0.52, size * 0.28, -size * 0.57)
    ctx.quadraticCurveTo(size * 0.62, -size * 0.64, size * 0.66, -size * 0.42)
    ctx.quadraticCurveTo(size * 0.7, size * 0.08, size * 0.18, size * 0.04)
    ctx.quadraticCurveTo(size * 0.16, size * 0.34, 0, size * 0.78)
    ctx.closePath()
    ctx.fillStyle = leafGradient
    ctx.fill()
    ctx.strokeStyle = 'rgba(38, 92, 54, .65)'
    ctx.lineWidth = Math.max(0.8, size * 0.045)
    ctx.stroke()
    ctx.strokeStyle = 'rgba(225, 243, 171, .58)'
    ctx.lineWidth = Math.max(0.7, size * 0.04)
    ctx.beginPath()
    ctx.moveTo(0, size * 0.66)
    ctx.lineTo(0, -size * 0.3)
    ctx.moveTo(0, -size * 0.08)
    ctx.lineTo(-size * 0.48, -size * 0.4)
    ctx.moveTo(0, -size * 0.08)
    ctx.lineTo(size * 0.48, -size * 0.4)
    ctx.moveTo(0, size * 0.22)
    ctx.lineTo(-size * 0.38, -size * 0.02)
    ctx.moveTo(0, size * 0.22)
    ctx.lineTo(size * 0.38, -size * 0.02)
    ctx.stroke()
  } else if (leaf.kind === 'needle') {
    ctx.strokeStyle = leaf.color
    ctx.lineWidth = Math.max(1, size * 0.08)
    for (let i = -2; i <= 2; i += 1) {
      ctx.beginPath()
      ctx.moveTo(0, i * size * 0.12)
      ctx.lineTo(size * 1.4, i * size * 0.12 - size * 0.45)
      ctx.stroke()
    }
  } else {
    const leafGradient = ctx.createLinearGradient(-size, 0, size, 0)
    leafGradient.addColorStop(0, 'rgba(75, 126, 77, .92)')
    leafGradient.addColorStop(0.45, leaf.color)
    leafGradient.addColorStop(1, 'rgba(196, 220, 139, .95)')
    ctx.beginPath()
    ctx.moveTo(-size * 1.08, 0)
    ctx.quadraticCurveTo(-size * 0.4, -size * 0.48, size * 0.95, -size * 0.08)
    ctx.quadraticCurveTo(size * 1.28, 0, size * 0.95, size * 0.08)
    ctx.quadraticCurveTo(-size * 0.4, size * 0.48, -size * 1.08, 0)
    ctx.fillStyle = leafGradient
    ctx.fill()
    ctx.strokeStyle = 'rgba(221, 242, 177, .5)'
    ctx.lineWidth = Math.max(0.7, size * 0.045)
    ctx.beginPath()
    ctx.moveTo(-size * 0.92, 0)
    ctx.lineTo(size * 0.98, 0)
    ctx.stroke()
    for (let i = -2; i <= 2; i += 1) {
      ctx.beginPath()
      ctx.moveTo(i * size * 0.18, 0)
      ctx.lineTo(i * size * 0.18 + size * 0.34, i % 2 ? -size * 0.17 : size * 0.17)
      ctx.stroke()
    }
  }
  ctx.restore()
}

function shouldAnimate() {
  return props.playing || dragging.value || (props.interactionMode === 'wind' && props.windIntensity > 0.001)
}

function cancelDraw() {
  if (animationFrame) cancelAnimationFrame(animationFrame)
  animationFrame = 0
}

function requestDraw() {
  if (animationFrame || !documentVisible) return
  animationFrame = requestAnimationFrame(draw)
}

function handleVisibilityChange() {
  documentVisible = !document.hidden
  if (documentVisible) requestDraw()
  else cancelDraw()
}

function draw(timestamp: number) {
  animationFrame = 0
  const canvas = canvasRef.value
  if (!canvas || !documentVisible) return
  const ctx = canvas.getContext('2d')
  if (!ctx) return
  const dpr = Math.min(window.devicePixelRatio || 1, 2)
  const width = canvas.width / dpr
  const height = canvas.height / dpr
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
  const elapsed = (timestamp - startedAt) / 1000
  const background = ctx.createLinearGradient(0, 0, 0, height)
  background.addColorStop(0, '#101d23')
  background.addColorStop(0.58, '#132a2a')
  background.addColorStop(1, '#0d171b')
  ctx.fillStyle = background
  ctx.fillRect(0, 0, width, height)

  const glow = ctx.createRadialGradient(width * 0.74, height * 0.2, 4, width * 0.74, height * 0.2, width * 0.62)
  glow.addColorStop(0, `rgba(244, 198, 105, ${0.13 + props.lightIntensity * 0.12})`)
  glow.addColorStop(1, 'rgba(244, 198, 105, 0)')
  ctx.fillStyle = glow
  ctx.fillRect(0, 0, width, height)

  ctx.strokeStyle = 'rgba(165, 214, 196, 0.10)'
  ctx.lineWidth = 1
  for (let i = -8; i < 12; i += 1) {
    ctx.beginPath()
    ctx.moveTo(width * 0.5 + i * 58, height * 0.71)
    ctx.lineTo(width * 0.5 + i * 130, height)
    ctx.stroke()
  }
  for (let i = 0; i < 5; i += 1) {
    const y = height * 0.71 + i * 32
    ctx.beginPath()
    ctx.moveTo(0, y)
    ctx.lineTo(width, y)
    ctx.stroke()
  }

  const scale = Math.min(width / 8.5, height / 6.2)
  ctx.save()
  ctx.beginPath()
  ctx.ellipse(width * 0.5 + view.panX, height * 0.735 + view.panY, scale * 1.7 * view.zoom, scale * 0.22 * view.zoom, 0, 0, Math.PI * 2)
  ctx.fillStyle = 'rgba(7, 16, 17, .34)'
  ctx.fill()
  ctx.restore()

  orderedBranches.value.forEach((branch) => drawBranch(ctx, branch, width, height, scale, elapsed))
  orderedLeaves.value.forEach((leaf) => drawLeaf(ctx, leaf, width, height, scale, elapsed))

  ctx.fillStyle = 'rgba(211, 238, 226, 0.68)'
  ctx.font = '11px "IBM Plex Mono", monospace'
  ctx.fillText(`RENDER PASS  /  ${scene.value.label}`, 24, height - 26)
  ctx.fillStyle = 'rgba(211, 238, 226, 0.4)'
  ctx.fillText(`LIGHT ${(props.lightIntensity * 100).toFixed(0)}%   WIND ${(props.windIntensity * 100).toFixed(0)}%`, width - 174, height - 26)
  const angle = ((Math.round((view.yaw * 180) / Math.PI) % 360) + 360) % 360
  ctx.fillStyle = 'rgba(211, 238, 226, 0.36)'
  ctx.fillText(`ORBIT ${angle}°   ZOOM ${view.zoom.toFixed(2)}×`, 24, 22)
  if (shouldAnimate()) requestDraw()
}

function resetView() {
  view.yaw = 0
  view.pitch = 0
  view.zoom = 1
  view.panX = 0
  view.panY = 0
  requestDraw()
}

function handlePointerDown(event: PointerEvent) {
  if (props.interactionMode === 'select' || (event.button !== 0 && event.button !== 1)) return
  const canvas = canvasRef.value
  if (!canvas) return
  dragState = { x: event.clientX, y: event.clientY, mode: event.button === 1 || event.shiftKey ? 'pan' : 'orbit' }
  dragging.value = true
  canvas.setPointerCapture(event.pointerId)
  requestDraw()
}

function handlePointerMove(event: PointerEvent) {
  if (!dragState) return
  const dx = event.clientX - dragState.x
  const dy = event.clientY - dragState.y
  dragState.x = event.clientX
  dragState.y = event.clientY
  if (dragState.mode === 'pan') {
    view.panX += dx
    view.panY += dy
  } else {
    view.yaw += dx * 0.012
    view.pitch = Math.max(-0.75, Math.min(0.75, view.pitch + dy * 0.009))
  }
  requestDraw()
}

function handlePointerUp(event: PointerEvent) {
  const canvas = canvasRef.value
  if (canvas?.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId)
  dragState = null
  dragging.value = false
  requestDraw()
}

function handleWheel(event: WheelEvent) {
  view.zoom = Math.max(0.55, Math.min(2.8, view.zoom * Math.exp(-event.deltaY * 0.001)))
  requestDraw()
}

onMounted(() => {
  const canvas = canvasRef.value
  if (!canvas) return
  startedAt = performance.now()
  documentVisible = !document.hidden
  resizeCanvas(canvas)
  observer = new ResizeObserver(() => {
    resizeCanvas(canvas)
    requestDraw()
  })
  observer.observe(canvas)
  document.addEventListener('visibilitychange', handleVisibilityChange)
  requestDraw()
})

onBeforeUnmount(() => {
  cancelDraw()
  observer?.disconnect()
  document.removeEventListener('visibilitychange', handleVisibilityChange)
})

watch(() => props.resetToken, resetView)
watch(() => [props.lightIntensity, props.windIntensity, props.playing, props.interactionMode, props.plantType, props.snapshot, props.growthProgress], requestDraw)
</script>

<template>
  <div class="viewport-shell">
    <canvas
      ref="canvasRef"
      class="viewport-canvas"
      :class="{ 'viewport-canvas--dragging': dragging }"
      aria-label="植物三维渲染预览"
      @pointerdown="handlePointerDown"
      @pointermove="handlePointerMove"
      @pointerup="handlePointerUp"
      @pointercancel="handlePointerUp"
      @wheel.prevent="handleWheel"
      @contextmenu.prevent
    ></canvas>
    <div class="viewport-corner viewport-corner--top">
      <span class="corner-dot"></span>
      <span>LIVE PREVIEW</span>
    </div>
    <div class="viewport-help">拖拽旋转 · Shift / 中键平移 · 滚轮缩放</div>
    <div class="viewport-axis">
      <span class="axis axis--x">X</span>
      <span class="axis axis--y">Y</span>
      <span class="axis axis--z">Z</span>
    </div>
  </div>
</template>
