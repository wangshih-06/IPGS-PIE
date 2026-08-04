<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'
import PlantViewport from './components/PlantViewport.vue'

type ConnectionState = 'connecting' | 'connected' | 'offline'
type InteractionMode = 'select' | 'orbit' | 'wind'
type LogItem = { time: string; message: string; tone?: 'success' | 'warn' | 'muted' }

const socket = ref<WebSocket | null>(null)
const connectionState = ref<ConnectionState>('connecting')
const lightIntensity = ref(0.8)
const moisture = ref(0.72)
const windIntensity = ref(0.28)
const playing = ref(true)
const activeTool = ref<InteractionMode>('orbit')
const viewportResetToken = ref(0)
const selectedPlant = ref('樱花树 / Spring Cherry')
const engineMessage = ref('等待 C++ 引擎握手')
const logs = ref<LogItem[]>([])
let reconnectTimer: ReturnType<typeof setTimeout> | undefined

const engineUrl = import.meta.env.VITE_ENGINE_WS_URL || 'ws://127.0.0.1:4317'
const connectionLabel = computed(() => {
  if (connectionState.value === 'connected') return '引擎已连接'
  if (connectionState.value === 'connecting') return '正在连接引擎'
  return '离线预览'
})
const lightPercent = computed(() => Math.round(lightIntensity.value * 100))
const nodeCount = computed(() => Math.round(128 + lightIntensity.value * 26 + windIntensity.value * 14))
const triangleCount = computed(() => Math.round(2480 + lightIntensity.value * 160))
const selectedPlantType = computed<'cherry' | 'willow' | 'pine' | 'passion'>(() => {
  if (selectedPlant.value.includes('柳')) return 'willow'
  if (selectedPlant.value.includes('松')) return 'pine'
  if (selectedPlant.value.includes('百香')) return 'passion'
  return 'cherry'
})

function timeStamp() {
  return new Intl.DateTimeFormat('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit' }).format(new Date())
}

function addLog(message: string, tone: LogItem['tone'] = 'muted') {
  logs.value.unshift({ time: timeStamp(), message, tone })
  logs.value = logs.value.slice(0, 4)
}

function connectEngine() {
  if (socket.value) socket.value.close()
  connectionState.value = 'connecting'
  engineMessage.value = '正在连接 C++ 引擎'
  const client = new WebSocket(engineUrl)
  socket.value = client
  client.onopen = () => {
    connectionState.value = 'connected'
    engineMessage.value = '连接成功'
    addLog('C++ 引擎握手成功', 'success')
  }
  client.onmessage = (event) => {
    try {
      const payload = JSON.parse(event.data) as { type?: string; message?: string; lightIntensity?: number }
      if (payload.type === 'environment_updated') {
        if (typeof payload.lightIntensity === 'number') lightIntensity.value = payload.lightIntensity
        engineMessage.value = payload.message || 'Environment Updated'
        addLog(`Light = ${lightIntensity.value.toFixed(1)}  /  Environment Updated`, 'success')
      }
    } catch {
      addLog('收到无法解析的引擎消息', 'warn')
    }
  }
  client.onerror = () => {
    connectionState.value = 'offline'
    engineMessage.value = '未检测到 C++ 引擎'
    addLog('WebSocket 暂不可用，已切换离线预览', 'warn')
  }
  client.onclose = () => {
    if (connectionState.value === 'connected') {
      connectionState.value = 'offline'
      engineMessage.value = '引擎连接已断开'
    }
  }
}

function send(command: Record<string, unknown>) {
  if (socket.value?.readyState === WebSocket.OPEN) {
    socket.value.send(JSON.stringify(command))
    return true
  }
  return false
}

function increaseLight() {
  const next = Math.min(1, Number((lightIntensity.value + 0.1).toFixed(2)))
  lightIntensity.value = next
  if (send({ type: 'adjust_light', value: next })) {
    addLog(`发送 Light = ${next.toFixed(1)}`, 'success')
    engineMessage.value = '等待环境更新回执'
  } else {
    addLog(`本地预览 Light = ${next.toFixed(1)}`, 'warn')
  }
}

function changeLight(event: Event) {
  const next = Number((event.target as HTMLInputElement).value)
  lightIntensity.value = next
  send({ type: 'adjust_light', value: next })
}

function toggleSimulation() {
  playing.value = !playing.value
  addLog(playing.value ? '模拟循环已恢复' : '模拟循环已暂停', 'muted')
}

function resetViewport() {
  viewportResetToken.value += 1
  addLog('视角已复位', 'muted')
}

function selectPlant(name: string) {
  selectedPlant.value = name
  addLog(`已载入预设：${name}`, 'muted')
}

onMounted(() => {
  connectEngine()
  addLog('控制台已就绪，等待引擎服务', 'muted')
})

onBeforeUnmount(() => {
  if (reconnectTimer) clearTimeout(reconnectTimer)
  socket.value?.close()
})
</script>

<template>
  <div class="app-frame">
    <aside class="sidebar">
      <div class="brand-lockup">
        <div class="brand-mark"><span></span><span></span><span></span></div>
        <div>
          <div class="brand-name">PLANTSIM</div>
          <div class="brand-subtitle">GROWTH LAB / 01</div>
        </div>
      </div>

      <nav class="primary-nav" aria-label="主导航">
        <button class="nav-item nav-item--active"><span class="nav-glyph nav-glyph--grid"></span>实验台</button>
        <button class="nav-item"><span class="nav-glyph nav-glyph--branch"></span>植物库</button>
        <button class="nav-item"><span class="nav-glyph nav-glyph--layers"></span>场景层级</button>
        <button class="nav-item"><span class="nav-glyph nav-glyph--sliders"></span>参数配置</button>
      </nav>

      <div class="sidebar-section">
        <div class="section-kicker">SCENE PRESETS <span>04</span></div>
        <button class="preset-item" :class="{ 'preset-item--active': selectedPlant.includes('樱花') }" @click="selectPlant('樱花树 / Spring Cherry')">
          <span class="preset-swatch preset-swatch--cherry"></span><span><strong>Spring Cherry</strong><small>花期 · 成熟期</small></span><span class="preset-check">✓</span>
        </button>
        <button class="preset-item" :class="{ 'preset-item--active': selectedPlant.includes('柳') }" @click="selectPlant('垂柳 / Weeping Willow')">
          <span class="preset-swatch preset-swatch--willow"></span><span><strong>Weeping Willow</strong><small>高分枝 · 柔性</small></span>
        </button>
        <button class="preset-item" :class="{ 'preset-item--active': selectedPlant.includes('松') }" @click="selectPlant('黑松 / Black Pine')">
          <span class="preset-swatch preset-swatch--pine"></span><span><strong>Black Pine</strong><small>向光性 · 稳定</small></span>
        </button>
        <button class="preset-item" :class="{ 'preset-item--active': selectedPlant.includes('百香') }" @click="selectPlant('百香果树 / Passion Fruit Vine')">
          <span class="preset-swatch preset-swatch--passion"></span><span><strong>Passion Fruit Vine</strong><small>攀援 · 花果期</small></span>
        </button>
      </div>

      <div class="sidebar-footer">
        <div class="engine-health"><span class="status-pulse" :class="`status-pulse--${connectionState}`"></span><span>{{ connectionLabel }}</span></div>
        <div class="build-stamp">BUILD 0.1.0 · LOCAL</div>
      </div>
    </aside>

    <main class="workspace">
      <header class="topbar">
        <div>
          <div class="eyebrow">PHYSICAL GROWTH SIMULATION / CONTROL ROOM</div>
          <h1>实验台 <span>/ {{ selectedPlant }}</span></h1>
        </div>
        <div class="topbar-actions">
          <div class="connection-badge" :class="`connection-badge--${connectionState}`"><span class="badge-dot"></span>{{ connectionLabel }}</div>
          <button class="icon-button" title="重新连接 C++ 引擎" aria-label="重新连接 C++ 引擎" @click="connectEngine"><span class="refresh-icon">↻</span></button>
          <button class="avatar-button" title="本地工作区">NL</button>
        </div>
      </header>

      <section class="metric-row" aria-label="运行指标">
        <article class="metric-item">
          <div class="metric-label">LIFE CYCLE</div><div class="metric-value">成熟期 <span class="metric-index">04 / 05</span></div><div class="metric-bar"><span style="width: 78%"></span></div>
        </article>
        <article class="metric-item">
          <div class="metric-label">ACTIVE NODES</div><div class="metric-value">{{ nodeCount }} <span class="metric-index">nodes</span></div><div class="metric-note metric-note--up">+12.4% <span>↗</span></div>
        </article>
        <article class="metric-item">
          <div class="metric-label">RENDER TIME</div><div class="metric-value">16.7 <span class="metric-index">ms / frame</span></div><div class="metric-note">60 FPS locked</div>
        </article>
        <article class="metric-item metric-item--signal">
          <div class="metric-label">ENGINE SIGNAL</div><div class="metric-value">{{ engineMessage }}</div><div class="signal-line"><span></span><span></span><span></span><span></span><span></span><span></span><span></span><span></span></div>
        </article>
      </section>

      <section class="studio-grid">
        <div class="viewport-panel">
          <div class="panel-heading"><div><span class="panel-index">01</span><span class="panel-title">三维视口</span></div><span class="panel-meta">OPENGL 4.3 / CORE PROFILE</span></div>
          <PlantViewport :light-intensity="lightIntensity" :wind-intensity="windIntensity" :playing="playing" :plant-type="selectedPlantType" :interaction-mode="activeTool" :reset-token="viewportResetToken" />
          <div class="viewport-toolbar">
            <div class="tool-group">
              <button class="tool-button" :class="{ 'tool-button--active': activeTool === 'select' }" title="选择节点" aria-label="选择节点" @click="activeTool = 'select'"><span class="tool-icon tool-icon--select"></span></button>
              <button class="tool-button" :class="{ 'tool-button--active': activeTool === 'orbit' }" title="轨道视角" aria-label="轨道视角" @click="activeTool = 'orbit'"><span class="tool-icon tool-icon--orbit"></span></button>
              <button class="tool-button" :class="{ 'tool-button--active': activeTool === 'wind' }" title="风场预览" aria-label="风场预览" @click="activeTool = 'wind'"><span class="tool-icon tool-icon--wind"></span></button>
            </div>
            <div class="viewport-actions"><span class="selection-readout">{{ activeTool.toUpperCase() }} MODE</span><button class="reset-button" title="复位视角" aria-label="复位视角" @click="resetViewport">↺</button></div>
          </div>
        </div>

        <aside class="inspector-panel">
          <div class="panel-heading"><div><span class="panel-index">02</span><span class="panel-title">环境参数</span></div><span class="live-label"><span class="live-dot"></span>LIVE</span></div>
          <div class="inspector-body">
            <div class="control-block control-block--highlight">
              <div class="control-title-row"><div><span class="control-label">光照强度</span><span class="control-caption">PHOTOTROPISM INPUT</span></div><strong>{{ lightPercent }}<small>%</small></strong></div>
              <input class="range-input range-input--amber" type="range" min="0" max="1" step="0.01" :value="lightIntensity" @input="changeLight" />
              <div class="range-labels"><span>LOW</span><span>DIRECT SUN</span></div>
              <button class="primary-action" @click="increaseLight"><span class="plus-symbol">+</span> 光照 +10% <span class="action-arrow">→</span></button>
            </div>
            <div class="control-block">
              <div class="control-title-row"><div><span class="control-label">风场强度</span><span class="control-caption">PERLIN FIELD / DYNAMIC</span></div><strong>{{ Math.round(windIntensity * 100) }}<small>%</small></strong></div>
              <input v-model.number="windIntensity" class="range-input range-input--mint" type="range" min="0" max="1" step="0.01" />
            </div>
            <div class="control-block">
              <div class="control-title-row"><div><span class="control-label">水分储备</span><span class="control-caption">ROOT UPTAKE</span></div><strong>{{ Math.round(moisture * 100) }}<small>%</small></strong></div>
              <input v-model.number="moisture" class="range-input range-input--blue" type="range" min="0" max="1" step="0.01" />
            </div>
            <div class="environment-list"><div><span class="mini-led mini-led--mint"></span>向光性</div><span>ACTIVE</span><div><span class="mini-led mini-led--blue"></span>向地性</div><span>STABLE</span><div><span class="mini-led mini-led--amber"></span>风场</div><span>{{ playing ? 'RUNNING' : 'PAUSED' }}</span></div>
          </div>
        </aside>
      </section>

      <section class="bottom-grid">
        <div class="growth-panel panel-lined">
          <div class="panel-heading"><div><span class="panel-index">03</span><span class="panel-title">生长控制</span></div><span class="panel-meta">T + 18.4 DAYS</span></div>
          <div class="growth-content"><div class="growth-timeline"><div class="timeline-track"><span class="timeline-progress"></span><i></i><i></i><i class="timeline-active"></i><i></i><i></i></div><div class="timeline-labels"><span>SEED</span><span>SPROUT</span><span>GROWING</span><span class="timeline-label--active">MATURE</span><span>AGING</span></div></div><div class="growth-actions"><button class="play-button" :class="{ 'play-button--active': playing }" title="播放或暂停模拟" aria-label="播放或暂停模拟" @click="toggleSimulation"><span :class="playing ? 'pause-icon' : 'play-icon'"></span></button><button class="secondary-action" @click="addLog('已重置到成熟期初始状态', 'muted')"><span>↺</span> 重置</button></div></div>
        </div>
        <div class="log-panel panel-lined">
          <div class="panel-heading"><div><span class="panel-index">04</span><span class="panel-title">事件流</span></div><span class="panel-meta">LAST 4 EVENTS</span></div>
          <div class="log-list"><div v-for="item in logs" :key="`${item.time}-${item.message}`" class="log-item"><span class="log-time">{{ item.time }}</span><span class="log-marker" :class="`log-marker--${item.tone || 'muted'}`"></span><span>{{ item.message }}</span></div><div v-if="!logs.length" class="empty-log">等待模拟事件</div></div>
        </div>
      </section>

      <footer class="statusbar"><span><i class="statusbar-dot"></i> LOCAL SESSION</span><span>WEBSOCKET / {{ engineUrl }}</span><span>GEOMETRY {{ triangleCount.toLocaleString() }} TRIS</span><span class="statusbar-right">SIMULATION CORE READY <b>·</b> {{ new Date().toLocaleDateString('zh-CN') }}</span></footer>
    </main>
  </div>
</template>
