<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import PlantViewport from './components/PlantViewport.vue'

type Connection = 'connecting' | 'connected' | 'reconnecting' | 'offline'
type Tool = 'select' | 'orbit' | 'wind'
type Metric = 'height' | 'totalBranchLength' | 'branchCount' | 'leafCount' | 'canopyWidth'
type Vector3 = [number, number, number]
type PlantSnapshot = {
  schema?: string
  plant?: { name?: string; species?: string; lifeStage?: string }
  skeleton?: {
    nodes?: Array<{ id: number; parentId: number; position: Vector3; radius?: number; active?: boolean; type?: string }>
    leaves?: Array<{ id: number; parentNodeId: number; position: Vector3; size?: [number, number]; active?: boolean }>
  }
}
type Point = { step: number; age: number; lifeStage: string; height: number; totalBranchLength: number; branchCount: number; leafCount: number; canopyWidth: number; plantState?: PlantSnapshot }
type State = Point & { speed: number; mode: number; nodeCount: number; recordedFrameCount: number; recordedEndAge: number }
type Log = { time: string; text: string; tone: 'ok' | 'warn' | 'muted' }
type Stage = { key: string; label: string; age: number }

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
const stageNames: Record<string, string> = { seedling: '\u5e7c\u82d7', vegetative: '\u8425\u517b\u751f\u957f', mature: '\u6210\u719f', completed: '\u5b8c\u6210', senescent: '\u8001\u5316' }
const stages = ref<Stage[]>(stageListFromFrames(history.value))
const chartHover = ref<Point | null>(null)
const label = computed(() => ({
  connecting: '正在连接引擎',
  connected: '引擎已连接',
  reconnecting: `正在重连（${reconnectDelay.value}s）`,
  offline: '离线回放预览',
}[connection.value]))
const maxAge = computed(() => Math.max(30, state.value.recordedEndAge, history.value.at(-1)?.age ?? 0))
const progress = computed(() => Math.min(100, age.value / maxAge.value * 100))
const current = computed(() => ({ height: state.value.height, totalBranchLength: state.value.totalBranchLength, branchCount: state.value.branchCount, leafCount: state.value.leafCount, canopyWidth: state.value.canopyWidth }))
const viewportGrowthProgress = computed(() => Math.max(0, Math.min(1, age.value / Math.max(0.001, maxAge.value))))
const path = computed(() => makePath(sample(history.value, 160), metric.value))
const area = computed(() => `${path.value} L 1000 240 L 0 240 Z`)
const chartMax = computed(() => Math.max(1, ...history.value.map(x => x[metric.value])))
const chartUnit = computed(() => metric.value === 'branchCount' || metric.value === 'leafCount' ? '' : ' m')
const chartPrecision = computed(() => metric.value === 'branchCount' || metric.value === 'leafCount' ? 0 : 2)
const chartTicks = computed(() => [chartMax.value, chartMax.value * .75, chartMax.value * .5, chartMax.value * .25, 0])
const chartCursorY = computed(() => 228 - current.value[metric.value] / chartMax.value * 198)
const chartHoverProgress = computed(() => chartHover.value ? chartHover.value.age / maxAge.value * 100 : 0)
const chartHoverY = computed(() => chartHover.value ? 228 - chartHover.value[metric.value] / chartMax.value * 198 : 228)
const isEngineConnected = computed(() => connection.value === 'connected')
let offlinePlaybackFrame = 0
let offlinePlaybackStartedAt = 0
let confirmationTimer = 0
let reconnectTimer = 0
let allowReconnect = true
const maxReconnectAttempts = 5

function offlineFrames(): Point[] { return Array.from({ length: 61 }, (_, step) => { const age = step / 2, g = 1 / (1 + Math.exp(-(age - 4.5) * .56)); return { step, age, lifeStage: age < .5 ? 'Seedling' : age < 3 ? 'Vegetative' : age < 20 ? 'Mature' : 'Senescent', height: +(.15 + 8.6 * g).toFixed(3), totalBranchLength: +(.2 + 112 * g ** 1.18).toFixed(3), branchCount: Math.round(2 + 174 * g ** 1.45), leafCount: Math.round(8 + 2630 * g ** 1.7), canopyWidth: +(.4 + 7.2 * g).toFixed(3) } }) }
function stageTitle(stage: Stage) { return stageNames[stage.key] ?? stage.label }
function stageListFromFrames(frames: Point[]): Stage[] {
  const stages: Stage[] = []
  for (const frame of frames) {
    const key = frame.lifeStage.trim().toLowerCase().replace(/[^a-z0-9]+/g, '-') || `stage-${frame.step}`
    if (!stages.some(stage => stage.key === key)) stages.push({ key, label: frame.lifeStage, age: frame.age })
  }
  return stages.length ? stages : [{ key: 'seedling', label: 'Seedling', age: 0 }]
}
function parseStages(value: unknown, frames: Point[]): Stage[] {
  if (Array.isArray(value)) {
    const parsed = value.map(item => {
      if (!item || typeof item !== 'object') return null
      const stage = item as Record<string, unknown>
      const age = asNumber(stage.age, NaN)
      const key = String(stage.key ?? '').trim()
      const label = String(stage.label ?? key).trim()
      return key && label && Number.isFinite(age) ? { key, label, age } : null
    }).filter((stage): stage is Stage => !!stage).sort((a, b) => a.age - b.age)
    if (parsed.length) return parsed
  }
  return stageListFromFrames(frames)
}
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
function asNumber(value: unknown, fallback = 0) { return typeof value === 'number' && Number.isFinite(value) ? value : fallback }
function toSnapshot(value: unknown): PlantSnapshot | undefined {
  if (!value || typeof value !== 'object') return undefined
  const snapshot = value as PlantSnapshot
  return snapshot.schema === 'plantsim.skeleton' && Array.isArray(snapshot.skeleton?.nodes) ? snapshot : undefined
}
function toPoint(value: Record<string, unknown>): Point | null { const m = value.metrics && typeof value.metrics === 'object' ? value.metrics as Record<string, unknown> : value; const a = asNumber(value.age, NaN); return Number.isFinite(a) ? { step: asNumber(value.step, history.value.length), age: a, lifeStage: String(value.lifeStage ?? 'Seedling'), height: asNumber(m.height), totalBranchLength: asNumber(m.totalBranchLength), branchCount: asNumber(m.branchCount), leafCount: asNumber(m.leafCount), canopyWidth: asNumber(m.canopyWidth), plantState: toSnapshot(value.plantState) } : null }
function nearest(target: number) { return history.value.reduce<Point | null>((best, x) => !best || Math.abs(x.age - target) < Math.abs(best.age - target) ? x : best, null) }
function localSeek(target: number) { const x = nearest(target); if (!x) return; age.value = x.age; state.value = { ...state.value, ...x, recordedFrameCount: history.value.length, recordedEndAge: history.value.at(-1)?.age ?? x.age } }
function append(x: Point) { const prev = history.value.at(-1); if (!prev || Math.abs(prev.age - x.age) > .0001) { if (prev && x.age < prev.age) history.value = history.value.filter(y => y.age <= x.age + .0001); history.value.push(x) } else history.value[history.value.length - 1] = x }
function receive(payload: Record<string, unknown>) { if (payload.type === 'error') { log(`引擎错误：${String(payload.message ?? '未知错误')}`, 'warn'); clearConfirmation(); return } if (payload.type === 'environment_updated') { light.value = asNumber(payload.lightIntensity, light.value); return } if (payload.type === 'growth_data' && Array.isArray(payload.frames)) { const frames = payload.frames.map(x => toPoint(x as Record<string, unknown>)).filter((x): x is Point => !!x); if (frames.length) { history.value = frames; stages.value = parseStages(payload.keyStages, frames); localSeek(age.value); log(`已载入 ${frames.length} 帧生长记录。`, 'ok') }; return } if (payload.type === 'growth_state') { const x = toPoint(payload); if (!x) return; state.value = { ...x, speed: asNumber(payload.speed, 1), mode: asNumber(payload.mode), nodeCount: asNumber(payload.nodeCount), recordedFrameCount: asNumber(payload.recordedFrameCount, history.value.length), recordedEndAge: asNumber(payload.recordedEndAge, x.age) }; age.value = x.age; speed.value = state.value.speed; playing.value = state.value.mode === 1; append(x); clearConfirmation() } }
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
  ws.onmessage = event => {
    if (socket.value !== ws) return
    try { receive(JSON.parse(event.data)) } catch { log('收到无法解析的引擎消息。', 'warn') }
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
function previewSeek(e: Event) { const target = +(e.target as HTMLInputElement).value; age.value = target; localSeek(target) }
function commitSeek(e: Event) { const target = +(e.target as HTMLInputElement).value; if (send({ type: 'growth_seek', age: target })) { awaitConfirmation('定位回放'); log(`已请求定位到 ${target.toFixed(2)} 年，等待引擎确认。`) } else log(`离线定位到 ${target.toFixed(2)} 年。`, 'ok') }
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
function setSpeed(e: Event) { const requested = +(e.target as HTMLInputElement).value; speed.value = requested; if (send({ type: 'growth_speed', speed: requested })) awaitConfirmation('设置回放速度') }
function setLight(e: Event) { light.value = +(e.target as HTMLInputElement).value; send({ type: 'adjust_light', value: light.value }) }
function formatMetric(value: number) { return value.toLocaleString('zh-CN', { maximumFractionDigits: chartPrecision.value, minimumFractionDigits: chartPrecision.value }) }
function chartPoint(event: MouseEvent) {
  const bounds = (event.currentTarget as SVGSVGElement).getBoundingClientRect()
  const ratio = Math.max(0, Math.min(1, (event.clientX - bounds.left) / Math.max(1, bounds.width)))
  return nearest(ratio * maxAge.value)
}
function previewChart(event: MouseEvent) { chartHover.value = chartPoint(event) }
function commitChartSeek(event: MouseEvent) {
  const point = chartPoint(event)
  if (!point) return
  chartHover.value = point
  age.value = point.age
  localSeek(point.age)
  if (send({ type: 'growth_seek', age: point.age })) { awaitConfirmation('chart seek'); log(`Chart seek requested at ${point.age.toFixed(2)}y.`) }
  else log(`Chart seek to ${point.age.toFixed(2)}y.`, 'ok')
}
function exportData(csv: boolean) { let text: string, type: string, name: string; if (csv) { const rows = ['step,age_years,life_stage,height,total_branch_length,branch_count,leaf_count,canopy_width', ...history.value.map(x => [x.step, x.age.toFixed(6), x.lifeStage, x.height.toFixed(6), x.totalBranchLength.toFixed(6), x.branchCount, x.leafCount, x.canopyWidth.toFixed(6)].join(','))]; text = `\uFEFF${rows.join('\n')}`; type = 'text/csv;charset=utf-8'; name = 'plant-growth-metrics.csv' } else { text = JSON.stringify({ schema: 'plantsim.growth_metrics.frontend', exportedAt: new Date().toISOString(), frameCount: history.value.length, frames: history.value }, null, 2); type = 'application/json'; name = 'plant-growth-metrics.json' }; const a = document.createElement('a'); a.href = URL.createObjectURL(new Blob([text], { type })); a.download = name; a.click(); URL.revokeObjectURL(a.href); log(`已导出生长指标 ${csv ? 'CSV' : 'JSON'}。`, 'ok') }
function sample<T>(items: T[], max: number) { if (items.length <= max) return items; const step = (items.length - 1) / (max - 1); return Array.from({ length: max }, (_, i) => items[Math.round(i * step)]) }
function makePath(items: Point[], key: Metric) { const end = Math.max(.001, maxAge.value), top = Math.max(1, ...items.map(x => x[key])); return items.map((x, i) => `${i ? 'L' : 'M'} ${(x.age / end * 1000).toFixed(2)} ${(228 - x[key] / top * 198).toFixed(2)}`).join(' ') }
onMounted(() => { localSeek(0); connect(true) }); onBeforeUnmount(() => { allowReconnect = false; clearReconnectTimer(); stopOfflinePlayback(); clearConfirmation(); socket.value?.close() })
</script>

<template>
  <div class="week12-shell">
    <header class="week12-header"><div><p class="eyebrow">WEEK 12 · GROWTH DATA LAB</p><h1>生长数据记录与回放</h1><p>逐时间步保存植物状态，回放生长过程并追踪结构指标变化。</p></div><div class="header-actions"><span class="chip" :class="connection"><i></i>{{ label }}</span><button class="ghost" @click="connect(true)">重新连接</button></div></header>
    <section class="metrics"><article class="stat accent"><span>生长年龄</span><strong>{{ state.age.toFixed(2) }}<small> 年</small></strong><em>{{ state.lifeStage }}</em></article><article class="stat"><span>植物高度</span><strong>{{ current.height.toFixed(2) }}<small> m</small></strong><em>HEIGHT</em></article><article class="stat"><span>枝干总长度</span><strong>{{ current.totalBranchLength.toFixed(1) }}<small> m</small></strong><em>BRANCH LENGTH</em></article><article class="stat"><span>分枝 / 叶片</span><strong>{{ current.branchCount }}<small> / {{ current.leafCount }}</small></strong><em>STRUCTURE</em></article><article class="stat"><span>冠幅</span><strong>{{ current.canopyWidth.toFixed(2) }}<small> m</small></strong><em>CANOPY WIDTH</em></article></section>
    <main class="main-grid"><section class="card viewport-card"><header><span>01</span><h2>植物生长预览</h2><b>{{ playing ? 'REPLAYING' : 'PAUSED' }}</b></header><PlantViewport :light-intensity="light" :wind-intensity="wind" :playing="playing" plant-type="cherry" :interaction-mode="tool" :reset-token="resetToken" :snapshot="state.plantState" :growth-progress="viewportGrowthProgress"/><footer><div class="tools"><button :class="{ active: tool === 'select' }" @click="tool = 'select'">选择</button><button :class="{ active: tool === 'orbit' }" @click="tool = 'orbit'">旋转</button><button :class="{ active: tool === 'wind' }" @click="tool = 'wind'">风场</button></div><label>光照 <input type="range" min="0" max="1" step=".01" :value="light" @input="setLight"/></label><button class="ghost" @click="resetToken += 1">复位视角</button></footer></section><aside class="card summary"><header><span>02</span><h2>记录摘要</h2><b>{{ state.recordedFrameCount }} FRAMES</b></header><dl><div><dt>记录频率</dt><dd>每个时间步</dd></div><div><dt>已记录时长</dt><dd>{{ state.recordedEndAge.toFixed(2) }} 年</dd></div><div><dt>回放速度</dt><dd>{{ speed.toFixed(1) }}×</dd></div><div><dt>活跃节点</dt><dd>{{ state.nodeCount }}</dd></div></dl><label class="speed">回放速度 <b>{{ speed.toFixed(1) }}×</b><input type="range" min=".1" max="8" step=".1" :value="speed" @input="setSpeed"/></label><div class="exports"><button class="primary" @click="exportData(false)">导出 JSON</button><button class="ghost" @click="exportData(true)">导出 CSV</button></div><p class="hint">引擎端保存完整植物状态快照；此处可下载用于曲线分析的指标数据。</p></aside></main>
    <section class="card timeline"><header><span>03</span><h2>生长过程回放</h2><b>T = {{ age.toFixed(2) }} / {{ maxAge.toFixed(0) }} 年</b></header><div class="timeline-row"><button class="play" :disabled="!!pendingAction" :aria-label="playing ? '暂停回放' : '开始回放'" @click="toggle"><i :class="playing ? 'pause' : 'triangle'"></i></button><div class="track"><input type="range" min="0" :max="maxAge" step=".01" :value="age" @input="previewSeek" @change="commitSeek"/><i :style="{ width: `${progress}%` }"></i></div><button class="ghost" @click="reset">重置记录</button></div><div class="stages"><button v-for="x in stages" :key="x.key" :class="{ active: Math.abs(age - x.age) < .35 }" @click="stage(x)"><i></i><span>{{ stageTitle(x) }}</span><small>{{ x.age }} 年</small></button></div></section>
    <section class="card chart"><header><span>04</span><h2>植物指标曲线</h2><nav><button v-for="(_, key) in names" :key="key" :class="{ active: metric === key }" @click="metric = key as Metric">{{ names[key as Metric] }}</button></nav></header><div class="chart-body"><div class="plot"><svg viewBox="0 0 1000 240" preserveAspectRatio="none" tabindex="0" aria-label="Plant growth metric chart. Move to inspect; click to seek." @mousemove="previewChart" @mouseleave="chartHover = null" @click="commitChartSeek"><defs><linearGradient id="fill" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#86db9b" stop-opacity=".38"/><stop offset="1" stop-color="#86db9b" stop-opacity="0"/></linearGradient></defs><path class="grid" d="M0 30H1000M0 80H1000M0 130H1000M0 180H1000M0 228H1000"/><path class="area" :d="area"/><path class="line" :d="path"/><line v-for="stagePoint in stages" :key="stagePoint.key" class="stage-cursor" :x1="stagePoint.age / maxAge * 1000" :x2="stagePoint.age / maxAge * 1000" y1="18" y2="228"/><line class="cursor" :x1="progress * 10" :x2="progress * 10" y1="18" y2="228"/><circle class="current-point" :cx="progress * 10" :cy="chartCursorY" r="6"/></svg><div v-if="chartHover" class="chart-tooltip" :style="{ left: `${chartHoverProgress}%`, top: `${chartHoverY}px` }"><b>{{ chartHover.age.toFixed(2) }} &#24180;</b><span>{{ chartHover.lifeStage }}</span><strong>{{ formatMetric(chartHover[metric]) }}{{ chartUnit }}</strong></div><div class="y-axis"><span v-for="value in chartTicks" :key="value">{{ formatMetric(value) }}{{ chartUnit }}</span></div><div class="axis"><span>0 年</span><span>{{ (maxAge / 2).toFixed(1) }} 年</span><span>{{ maxAge.toFixed(0) }} 年</span></div></div><aside><span>当前指标</span><strong>{{ current[metric].toFixed(metric === 'branchCount' || metric === 'leafCount' ? 0 : 2) }}<small>{{ ['branchCount','leafCount'].includes(metric) ? ' 个' : ' m' }}</small></strong><p>{{ names[metric] }}曲线由 {{ history.length }} 个记录时间步生成。最大值 {{ chartMax.toFixed(1) }}。</p></aside></div></section>
    <section class="card events"><header><span>05</span><h2>回放事件</h2></header><div v-if="logs.length" class="event-list"><div v-for="x in logs" :key="x.time + x.text"><time>{{ x.time }}</time><i :class="x.tone"></i><p>{{ x.text }}</p></div></div><p v-else class="empty">等待生长事件…</p></section>
  </div>
</template>
