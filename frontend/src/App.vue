<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import PlantViewport from './components/PlantViewport.vue'
import GrowthChart from './components/GrowthChart.vue'
import GrowthTimeline from './components/GrowthTimeline.vue'
import MetricsPanel from './components/MetricsPanel.vue'
import ReplayEventLog from './components/ReplayEventLog.vue'
import {
  appendFrame,
  asNumber,
  findNearest,
  normalizeFrames,
  offlineFrames,
  parseStages,
  stageListFromFrames,
  stageTitle,
  toPoint,
  type Metric,
  type Point,
  type Stage,
} from './lib/growthData'
import { downloadGrowthExport, makeGrowthExport } from './lib/growthExport'

type Connection = 'connecting' | 'connected' | 'reconnecting' | 'offline'
type Tool = 'select' | 'orbit' | 'wind'
type State = Point & { speed: number; mode: number; nodeCount: number; recordedFrameCount: number; recordedEndAge: number }
type Log = { time: string; text: string; tone: 'ok' | 'warn' | 'muted' }

const engineUrl = import.meta.env.VITE_ENGINE_WS_URL || 'ws://127.0.0.1:4317'
const socket = ref<WebSocket | null>(null)
const connection = ref<Connection>('connecting')
const light = ref(.8), wind = ref(.28), tool = ref<Tool>('orbit'), resetToken = ref(0), playing = ref(false), speed = ref(1), age = ref(0), metric = ref<Metric>('height')
const logs = ref<Log[]>([])
const reconnectAttempt = ref(0)
const reconnectDelay = ref(0)
const pendingAction = ref<string | null>(null)
const history = ref<Point[]>(offlineFrames())
const state = ref<State>({ ...history.value[0], speed: 1, mode: 0, nodeCount: 1, recordedFrameCount: history.value.length, recordedEndAge: 30 })
const names: Record<Metric, string> = { height: '植物高度', totalBranchLength: '枝干总长度', branchCount: '分枝数量', leafCount: '叶片数量', canopyWidth: '冠幅' }
const stages = ref<Stage[]>(stageListFromFrames(history.value))
const label = computed(() => ({
  connecting: '正在连接引擎',
  connected: '引擎已连接',
  reconnecting: `正在重连（${reconnectDelay.value}s）`,
  offline: '离线回放预览',
}[connection.value]))
const maxAge = computed(() => Math.max(30, state.value.recordedEndAge, history.value.at(-1)?.age ?? 0))
const atLatest = computed(() => Math.abs(age.value - (history.value.at(-1)?.age ?? maxAge.value)) < 0.0001)
const connectionNote = computed(() => {
  if (pendingAction.value) return `正在等待引擎确认：${pendingAction.value}`
  if (connection.value === 'connecting') return '正在建立与引擎的连接。'
  if (connection.value === 'offline') return '当前为离线回放，新操作不会同步到引擎。'
  if (connection.value === 'reconnecting') return '正在恢复与引擎的连接。'
  return '记录和回放状态已与引擎同步。'
})
const progress = computed(() => Math.min(100, age.value / maxAge.value * 100))
const current = computed(() => ({ height: state.value.height, totalBranchLength: state.value.totalBranchLength, branchCount: state.value.branchCount, leafCount: state.value.leafCount, canopyWidth: state.value.canopyWidth }))
const viewportGrowthProgress = computed(() => Math.max(0, Math.min(1, age.value / Math.max(0.001, maxAge.value))))
const isEngineConnected = computed(() => connection.value === 'connected')
let offlinePlaybackFrame = 0
let offlinePlaybackStartedAt = 0
let confirmationTimer = 0
let reconnectTimer = 0
let allowReconnect = true
const maxReconnectAttempts = 5

function log(text: string, tone: Log['tone'] = 'muted') { logs.value.unshift({ time: new Intl.DateTimeFormat('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' }).format(new Date()), text, tone }); logs.value = logs.value.slice(0, 6) }
function send(command: Record<string, unknown>) { try { if (socket.value?.readyState !== WebSocket.OPEN) return false; socket.value.send(JSON.stringify(command)); return true } catch { return false } }
function clearConfirmation() { if (confirmationTimer) window.clearTimeout(confirmationTimer); confirmationTimer = 0; pendingAction.value = null }
function awaitConfirmation(action: string) {
  if (confirmationTimer) window.clearTimeout(confirmationTimer)
  pendingAction.value = action
  confirmationTimer = window.setTimeout(() => {
    if (!pendingAction.value) return
    log(`引擎尚未确认“${pendingAction.value}”操作，界面将继续以最后接收的状态为准。`, 'warn')
    pendingAction.value = null
    confirmationTimer = 0
  }, 2500)
}
function stopOfflinePlayback() { if (offlinePlaybackFrame) cancelAnimationFrame(offlinePlaybackFrame); offlinePlaybackFrame = 0; offlinePlaybackStartedAt = 0 }
function runOfflinePlayback(timestamp: number) {
  if (isEngineConnected.value || !playing.value) { stopOfflinePlayback(); return }
  if (!offlinePlaybackStartedAt) offlinePlaybackStartedAt = timestamp
  const deltaSeconds = Math.max(0, (timestamp - offlinePlaybackStartedAt) / 1000)
  offlinePlaybackStartedAt = timestamp
  const nextAge = Math.min(maxAge.value, age.value + deltaSeconds * 0.75 * speed.value)
  localSeek(nextAge)
  if (nextAge >= maxAge.value - 0.0001) { playing.value = false; stopOfflinePlayback(); log('离线回放已到达最后一个记录帧。', 'ok'); return }
  offlinePlaybackFrame = requestAnimationFrame(runOfflinePlayback)
}
function startOfflinePlayback() { stopOfflinePlayback(); offlinePlaybackFrame = requestAnimationFrame(runOfflinePlayback) }
function nearest(target: number) { return findNearest(history.value, target) }
function localSeek(target: number) { const x = nearest(target); if (!x) return; age.value = x.age; const point = { ...x, plantState: x.plantState ?? state.value.plantState }; state.value = { ...state.value, ...point, recordedFrameCount: history.value.length, recordedEndAge: history.value.at(-1)?.age ?? point.age } }
function append(x: Point) { history.value = appendFrame(history.value, x) }
async function decodeEnginePayload(payload: unknown): Promise<Record<string, unknown>> {
  if (typeof payload === 'string') return JSON.parse(payload) as Record<string, unknown>
  if (!(payload instanceof Blob)) throw new Error('Unsupported WebSocket payload')
  const bytes = new Uint8Array(await payload.arrayBuffer())
  const marker = new TextDecoder().decode(bytes.slice(0, 4))
  if (marker !== 'PSZ1') throw new Error('Unknown binary WebSocket payload')
  if (typeof DecompressionStream === 'undefined') throw new Error('Browser does not support compressed engine messages')
  // qCompress stores a four-byte original-size prefix before its zlib stream.
  const zlibPayload = bytes.slice(8)
  const decompressed = await new Response(new Blob([zlibPayload]).stream().pipeThrough(new DecompressionStream('deflate'))).arrayBuffer()
  return JSON.parse(new TextDecoder().decode(decompressed)) as Record<string, unknown>
}
function receive(payload: Record<string, unknown>) { if (payload.type === 'error') { log(`引擎错误：${String(payload.message ?? '未知错误')}`, 'warn'); clearConfirmation(); return } if (payload.type === 'environment_updated') { light.value = asNumber(payload.lightIntensity, light.value); return } if (payload.type === 'growth_data' && Array.isArray(payload.frames)) { const frames = normalizeFrames(payload.frames.map((x, index) => toPoint(x, index)).filter((x): x is Point => !!x)); if (frames.length) { history.value = frames; stages.value = parseStages(payload.keyStages, frames); localSeek(age.value); log(`已载入 ${frames.length} 帧生长记录。`, 'ok') }; return } if (payload.type === 'growth_state') { const x = toPoint(payload); if (!x) return; const point = { ...x, plantState: x.plantState ?? state.value.plantState }; state.value = { ...point, speed: asNumber(payload.speed, 1), mode: asNumber(payload.mode), nodeCount: asNumber(payload.nodeCount), recordedFrameCount: asNumber(payload.recordedFrameCount, history.value.length), recordedEndAge: asNumber(payload.recordedEndAge, point.age) }; age.value = point.age; speed.value = state.value.speed; playing.value = state.value.mode === 1; append(point); clearConfirmation() } }
function clearReconnectTimer() { if (reconnectTimer) window.clearTimeout(reconnectTimer); reconnectTimer = 0 }
function scheduleReconnect(reason: string) {
  if (!allowReconnect || reconnectTimer || isEngineConnected.value) return
  if (reconnectAttempt.value >= maxReconnectAttempts) {
    connection.value = 'offline'
    reconnectDelay.value = 0
    log(`引擎连接已断开（${reason}），自动重连已暂停；可手动重新连接或继续离线回放。`, 'warn')
    return
  }
  reconnectAttempt.value += 1
  reconnectDelay.value = Math.min(10, 2 ** (reconnectAttempt.value - 1))
  connection.value = 'reconnecting'
  log(`引擎连接${reason}，${reconnectDelay.value} 秒后进行第 ${reconnectAttempt.value}/${maxReconnectAttempts} 次重连。`, 'warn')
  reconnectTimer = window.setTimeout(() => { reconnectTimer = 0; connect(false) }, reconnectDelay.value * 1000)
}
function connect(manual = true) {
  if (manual) {
    reconnectAttempt.value = 0
    reconnectDelay.value = 0
    clearConfirmation()
  }
  clearReconnectTimer()
  const previous = socket.value
  socket.value = null
  previous?.close()
  connection.value = manual ? 'connecting' : 'reconnecting'
  let ws: WebSocket
  try { ws = new WebSocket(engineUrl) }
  catch { scheduleReconnect('创建失败'); return }
  socket.value = ws
  ws.onopen = () => {
    if (socket.value !== ws) return
    connection.value = 'connected'
    reconnectAttempt.value = 0
    reconnectDelay.value = 0
    send({ type: 'request_growth_data' })
    log('已连接 C++ 生长引擎。', 'ok')
  }
  ws.binaryType = 'blob'
  ws.onmessage = async event => {
    if (socket.value !== ws) return
    try { receive(await decodeEnginePayload(event.data)) } catch { log('收到无法解析的引擎消息。', 'warn') }
  }
  ws.onerror = () => { /* close 事件统一处理，避免重复提示。 */ }
  ws.onclose = () => {
    if (socket.value !== ws) return
    socket.value = null
    clearConfirmation()
    playing.value = false
    scheduleReconnect('已关闭')
  }
}
function previewSeek(target: number) { age.value = target; localSeek(target) }
function commitSeek(target: number) { if (send({ type: 'growth_seek', age: target })) { awaitConfirmation('\u5b9a\u4f4d\u56de\u653e'); log(`\u5df2\u8bf7\u6c42\u5b9a\u4f4d\u5230 ${target.toFixed(2)} \u5e74\uff0c\u7b49\u5f85\u5f15\u64ce\u786e\u8ba4\u3002`) } else log(`\u79bb\u7ebf\u5b9a\u4f4d\u5230 ${target.toFixed(2)} \u5e74\u3002`, 'ok') }
function stage(x: Stage) { age.value = x.age; localSeek(x.age); if (send({ type: 'growth_seek', age: x.age })) { awaitConfirmation(`跳转${stageTitle(x)}`); log(`已请求跳转到关键阶段：${stageTitle(x)}。`) } else log(`离线跳转到关键阶段：${stageTitle(x)}。`, 'ok') }
function toggle() {
  if (isEngineConnected.value) {
    const command = playing.value ? 'growth_pause' : 'growth_resume'
    if (send({ type: command })) { awaitConfirmation(playing.value ? '暂停回放' : '开始回放'); log(`已请求${playing.value ? '暂停' : '开始'}生长过程回放。`) }
    return
  }
  if (playing.value) { playing.value = false; stopOfflinePlayback(); log('已暂停离线回放。') }
  else { if (age.value >= maxAge.value - 0.0001) localSeek(0); playing.value = true; startOfflinePlayback(); log('开始离线生长过程回放。', 'ok') }
}
function reset() { age.value = 0; localSeek(0); if (send({ type: 'growth_reset' })) { awaitConfirmation('重置回放'); log('已请求重置生长记录。') } else { playing.value = false; stopOfflinePlayback(); log('已重置离线回放。', 'ok') } }
function jumpToLatest() {
  const target = history.value.at(-1)?.age ?? maxAge.value
  age.value = target
  localSeek(target)
  if (send({ type: 'growth_seek', age: target })) { awaitConfirmation('\u56de\u5230\u6700\u65b0\u5e27'); log(`\u5df2\u8bf7\u6c42\u8fd4\u56de\u6700\u65b0\u8bb0\u5f55\u5e27\uff1a${target.toFixed(2)} \u5e74\u3002`) }
  else log(`\u5df2\u8fd4\u56de\u6700\u65b0\u8bb0\u5f55\u5e27\uff1a${target.toFixed(2)} \u5e74\u3002`, 'ok')
}
function setSpeed(requested: number) { speed.value = requested; if (send({ type: 'growth_speed', speed: requested })) awaitConfirmation('\u8bbe\u7f6e\u56de\u653e\u901f\u5ea6') }
function setLight(e: Event) { light.value = +(e.target as HTMLInputElement).value; send({ type: 'adjust_light', value: light.value }) }
function commitChartSeek(target: number) {
  age.value = target
  localSeek(target)
  if (send({ type: 'growth_seek', age: target })) { awaitConfirmation('\u56fe\u8868\u5b9a\u4f4d'); log(`\u5df2\u8bf7\u6c42\u4ece\u56fe\u8868\u5b9a\u4f4d\u5230 ${target.toFixed(2)} \u5e74\u3002`) }
  else log(`\u5df2\u4ece\u56fe\u8868\u5b9a\u4f4d\u5230 ${target.toFixed(2)} \u5e74\u3002`, 'ok')
}
function exportData(kind: 'json' | 'csv') { downloadGrowthExport(makeGrowthExport(kind, history.value)); log(`\u5df2\u5bfc\u51fa\u751f\u957f\u6307\u6807 ${kind.toUpperCase()}\u3002`, 'ok') }
onMounted(() => { localSeek(0); connect(true) }); onBeforeUnmount(() => { allowReconnect = false; clearReconnectTimer(); stopOfflinePlayback(); clearConfirmation(); socket.value?.close() })
</script>

<template>
  <div class="week12-shell">
    <header class="week12-header"><div><p class="eyebrow">WEEK 12 · GROWTH DATA LAB</p><h1>生长数据记录与回放</h1><p>逐时间步保存植物状态，回放生长过程并追踪结构指标变化。</p><p class="connection-note" aria-live="polite">{{ connectionNote }}</p></div><div class="header-actions"><span class="chip" :class="connection" role="status"><i></i>{{ label }}</span><button type="button" class="ghost" @click="connect(true)">重新连接</button></div></header>
    <section class="metrics"><article class="stat accent"><span>生长年龄</span><strong>{{ state.age.toFixed(2) }}<small> 年</small></strong><em>{{ state.lifeStage }}</em></article><article class="stat"><span>植物高度</span><strong>{{ current.height.toFixed(2) }}<small> m</small></strong><em>HEIGHT</em></article><article class="stat"><span>枝干总长度</span><strong>{{ current.totalBranchLength.toFixed(1) }}<small> m</small></strong><em>BRANCH LENGTH</em></article><article class="stat"><span>分枝 / 叶片</span><strong>{{ current.branchCount }}<small> / {{ current.leafCount }}</small></strong><em>STRUCTURE</em></article><article class="stat"><span>冠幅</span><strong>{{ current.canopyWidth.toFixed(2) }}<small> m</small></strong><em>CANOPY WIDTH</em></article></section>
    <main class="main-grid"><section class="card viewport-card"><header><span>01</span><h2>植物生长预览</h2><b>{{ playing ? 'REPLAYING' : 'PAUSED' }}</b></header><PlantViewport :light-intensity="light" :wind-intensity="wind" :playing="playing" plant-type="cherry" :interaction-mode="tool" :reset-token="resetToken" :snapshot="state.plantState" :growth-progress="viewportGrowthProgress"/><footer><div class="tools"><button type="button" :class="{ active: tool === 'select' }" :aria-pressed="tool === 'select'" @click="tool = 'select'">选择</button><button type="button" :class="{ active: tool === 'orbit' }" :aria-pressed="tool === 'orbit'" @click="tool = 'orbit'">旋转</button><button type="button" :class="{ active: tool === 'wind' }" :aria-pressed="tool === 'wind'" @click="tool = 'wind'">风场</button></div><label>光照 <input type="range" min="0" max="1" step=".01" :value="light" @input="setLight"/></label><button type="button" class="ghost" @click="resetToken += 1">复位视角</button></footer></section><MetricsPanel :recorded-frame-count="state.recordedFrameCount" :recorded-end-age="state.recordedEndAge" :speed="speed" :node-count="state.nodeCount" @speed-change="setSpeed" @export="exportData"/></main>
    <GrowthTimeline :age="age" :max-age="maxAge" :progress="progress" :playing="playing" :pending="!!pendingAction" :at-latest="atLatest" :stages="stages" @toggle="toggle" @preview="previewSeek" @seek="commitSeek" @reset="reset" @latest="jumpToLatest" @stage="stage"/>
    <GrowthChart :history="history" :metric="metric" :metric-names="names" :current-value="current[metric]" :age="age" :max-age="maxAge" :stages="stages" @update:metric="metric = $event" @seek="commitChartSeek"/>
    <ReplayEventLog :logs="logs"/>
  </div>
</template>
