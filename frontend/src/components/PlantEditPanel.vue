<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue'

type EditTool = 'scale' | 'bend' | 'rotate' | 'parameter'
type NodeSnapshot = {
  id: number
  parentId?: number
  depth?: number
  length?: number
  radius?: number
  age?: number
  generation?: number
}
type PlantSnapshot = { skeleton?: { nodes?: NodeSnapshot[] } }
type EditPayload = {
  nodeId: number
  mode: 'node'
  tool: EditTool
  preview?: boolean
  params?: Record<string, unknown>
}

const props = withDefaults(defineProps<{
  snapshot?: PlantSnapshot
  connected: boolean
  canUndo: boolean
}>(), { snapshot: undefined, connected: false, canUndo: false })
const emit = defineEmits<{
  begin: [payload: EditPayload]
  update: [payload: EditPayload]
  commit: [payload: EditPayload]
  undo: []
  reset: []
}>()

const tool = ref<EditTool>('scale')
const nodeId = ref(0)
const editing = ref(false)
const scale = ref(1)
const scaleAxis = ref<'uniform' | 'x' | 'y' | 'z'>('uniform')
const bendAngle = ref(0)
const bendAxis = ref<'x' | 'y' | 'z'>('z')
const rotateAngle = ref(0)
const rotateAxis = ref<'x' | 'y' | 'z'>('y')
const length = ref(0.5)
const radius = ref(0.05)
const leafDensity = ref(1)
const nodeAge = ref(0)
const growthDepth = ref(0)
let previewFrame = 0

const nodes = computed(() => props.snapshot?.skeleton?.nodes ?? [])
const selected = computed(() => nodes.value.find((node) => node.id === nodeId.value))
const editingLabel = computed(() => editing.value ? 'Preview active' : 'Ready')

watch(nodes, (current) => {
  if (!current.length) return
  if (!current.some((node) => node.id === nodeId.value)) nodeId.value = current[0].id
}, { immediate: true })
watch(selected, (node) => {
  if (!node) return
  if (typeof node.length === 'number') length.value = node.length
  if (typeof node.radius === 'number') radius.value = node.radius
  if (typeof node.age === 'number') nodeAge.value = node.age
  if (typeof node.generation === 'number') growthDepth.value = node.generation
}, { immediate: true })

function axisVector(axis: 'x' | 'y' | 'z') {
  return axis === 'x' ? { x: 1, y: 0, z: 0 } : axis === 'y' ? { x: 0, y: 1, z: 0 } : { x: 0, y: 0, z: 1 }
}
function parameters(): Record<string, unknown> {
  if (tool.value === 'scale') return { scale: scale.value, axis: scaleAxis.value, scaleLeaves: true }
  if (tool.value === 'bend') return { angleDegrees: bendAngle.value, axis: axisVector(bendAxis.value), stiffness: 1, falloff: 1 }
  if (tool.value === 'rotate') return { angleDegrees: rotateAngle.value, axis: axisVector(rotateAxis.value) }
  return { length: length.value, radius: radius.value, leafDensity: leafDensity.value, age: nodeAge.value, growthDepth: Math.round(growthDepth.value) }
}
function payload(preview?: boolean): EditPayload {
  return { nodeId: Math.max(0, Math.round(nodeId.value)), mode: 'node', tool: tool.value, preview, params: parameters() }
}
function queuePreview() {
  if (!editing.value) return
  if (previewFrame) return
  previewFrame = requestAnimationFrame(() => {
    previewFrame = 0
    emit('update', payload(true))
  })
}
function begin() {
  if (!props.connected || editing.value) return
  editing.value = true
  emit('begin', payload())
  queuePreview()
}
function commit() {
  if (!editing.value) return
  if (previewFrame) cancelAnimationFrame(previewFrame)
  previewFrame = 0
  emit('update', payload(false))
  emit('commit', payload(false))
  editing.value = false
}
function undo() {
  if (!props.connected) return
  editing.value = false
  emit('undo')
}
function reset() {
  if (!props.connected) return
  editing.value = false
  emit('reset')
}
onBeforeUnmount(() => { if (previewFrame) cancelAnimationFrame(previewFrame) })
</script>

<template>
  <section class="edit-panel card">
    <header><span>02</span><h2>Interactive editing</h2><b :class="{ active: editing }">{{ editingLabel }}</b></header>
    <div class="edit-body">
      <label>Node ID
        <input v-model.number="nodeId" type="number" min="0" step="1" :disabled="!connected || editing" />
      </label>
      <p v-if="selected" class="node-summary">Parent {{ selected.parentId ?? -1 }} ? depth {{ selected.depth ?? 0 }} ? length {{ (selected.length ?? 0).toFixed(3) }} ? radius {{ (selected.radius ?? 0).toFixed(3) }}</p>
      <p v-else class="node-summary">Enter a C++ PlantModel node ID. Node details appear once a full plant snapshot is synchronized.</p>

      <fieldset :disabled="!connected">
        <legend>Tool</legend>
        <div class="tool-grid">
          <button v-for="item in (['scale', 'bend', 'rotate', 'parameter'] as EditTool[])" :key="item" type="button" :class="{ active: tool === item }" :disabled="editing" @click="tool = item">{{ { scale: 'Scale', bend: 'Bend', rotate: 'Rotate', parameter: 'Parameters' }[item] }}</button>
        </div>
      </fieldset>

      <div v-if="tool === 'scale'" class="parameters">
        <label>Scale {{ scale.toFixed(2) }}<input v-model.number="scale" type="range" min="0.2" max="2" step="0.01" @input="queuePreview" /></label>
        <label>Axis<select v-model="scaleAxis" @change="queuePreview"><option value="uniform">Uniform</option><option value="x">X</option><option value="y">Y</option><option value="z">Z</option></select></label>
      </div>
      <div v-else-if="tool === 'bend'" class="parameters">
        <label>Bend angle {{ bendAngle.toFixed(0) }}?<input v-model.number="bendAngle" type="range" min="-75" max="75" step="1" @input="queuePreview" /></label>
        <label>Bend axis<select v-model="bendAxis" @change="queuePreview"><option value="x">X</option><option value="y">Y</option><option value="z">Z</option></select></label>
      </div>
      <div v-else-if="tool === 'rotate'" class="parameters">
        <label>Rotation {{ rotateAngle.toFixed(0) }}?<input v-model.number="rotateAngle" type="range" min="-180" max="180" step="1" @input="queuePreview" /></label>
        <label>Rotation axis<select v-model="rotateAxis" @change="queuePreview"><option value="x">X</option><option value="y">Y</option><option value="z">Z</option></select></label>
      </div>
      <div v-else class="parameters parameter-grid">
        <label>Length<input v-model.number="length" type="number" min="0.001" step="0.01" @input="queuePreview" /></label>
        <label>Radius<input v-model.number="radius" type="number" min="0.001" step="0.005" @input="queuePreview" /></label>
        <label>Leaf density<input v-model.number="leafDensity" type="number" min="0" max="1" step="0.05" @input="queuePreview" /></label>
        <label>Age<input v-model.number="nodeAge" type="number" min="0" step="0.1" @input="queuePreview" /></label>
        <label>Growth depth<input v-model.number="growthDepth" type="number" min="0" step="1" @input="queuePreview" /></label>
      </div>

      <div class="actions">
        <button type="button" class="primary" :disabled="!connected || editing" @click="begin">Reset plant</button>
        <button type="button" :disabled="!connected || !editing" @click="commit">Reset plant</button>
        <button type="button" :disabled="!connected || !canUndo" @click="undo">Undo</button>
        <button type="button" class="danger" :disabled="!connected" @click="reset">Reset plant</button>
      </div>
    </div>
  </section>
</template>

<style scoped>
.edit-panel { min-width: 0; }
header { display:flex; align-items:center; gap:10px; }
header span { color:#7bd6bc; font:600 12px/1 monospace; }
header h2 { flex:1; margin:0; font-size:15px; }
header b { color:#9aaba4; font-size:11px; } header b.active { color:#71d6aa; }
.edit-body { display:grid; gap:10px; padding-top:12px; }
label { display:grid; gap:5px; color:#b9cac2; font-size:12px; }
input, select { width:100%; box-sizing:border-box; border:1px solid #28443a; border-radius:6px; background:#0b1915; color:#edf6ef; padding:7px; }
input[type='range'] { padding:0; accent-color:#64cfa6; }
fieldset { border:0; margin:0; padding:0; } legend { margin-bottom:6px; color:#b9cac2; font-size:12px; }
.tool-grid, .actions { display:grid; grid-template-columns:repeat(2, minmax(0, 1fr)); gap:6px; }
button { border:1px solid #315547; border-radius:6px; background:#10251e; color:#dcefe5; padding:8px; cursor:pointer; }
button.active, button.primary { border-color:#66d7aa; background:#1a5843; } button.danger { border-color:#94575b; color:#ffd9d9; } button:disabled { cursor:not-allowed; opacity:.46; }
.node-summary { margin:0; color:#8ca49a; font-size:11px; line-height:1.5; }
.parameters { display:grid; gap:9px; }.parameter-grid { grid-template-columns:repeat(2, minmax(0, 1fr)); }
.actions { grid-template-columns:repeat(4, minmax(0, 1fr)); }
@media (max-width: 760px) { .actions { grid-template-columns:repeat(2, minmax(0, 1fr)); } }
</style>
