<script setup lang="ts">
import { computed, ref } from 'vue'
import { findNearest, makePath, sample, type Metric, type Point, type Stage } from '../lib/growthData'

const props = defineProps<{
  history: Point[]
  metric: Metric
  metricNames: Record<Metric, string>
  currentValue: number
  age: number
  maxAge: number
  stages: Stage[]
}>()

const emit = defineEmits<{
  'update:metric': [metric: Metric]
  seek: [age: number]
}>()

const hover = ref<Point | null>(null)
const isCountMetric = computed(() => props.metric === 'branchCount' || props.metric === 'leafCount')
const chartMax = computed(() => Math.max(1, ...props.history.map(frame => frame[props.metric])))
const path = computed(() => makePath(sample(props.history, 160), props.metric, props.maxAge, chartMax.value))
const area = computed(() => `${path.value} L 1000 240 L 0 240 Z`)
const unit = computed(() => isCountMetric.value ? '' : ' m')
const currentUnit = computed(() => isCountMetric.value ? ' \u4e2a' : ' m')
const precision = computed(() => isCountMetric.value ? 0 : 2)
const ticks = computed(() => [chartMax.value, chartMax.value * .75, chartMax.value * .5, chartMax.value * .25, 0])
const cursorY = computed(() => 228 - props.currentValue / chartMax.value * 198)
const hoverProgress = computed(() => hover.value ? hover.value.age / props.maxAge * 100 : 0)
const hoverY = computed(() => hover.value ? 228 - hover.value[props.metric] / chartMax.value * 198 : 228)
const ui = {
  title: '\u690d\u7269\u6307\u6807\u66f2\u7ebf',
  currentMetric: '\u5f53\u524d\u6307\u6807',
  curveFrom: '\u66f2\u7ebf\u7531',
  timeSteps: '\u4e2a\u8bb0\u5f55\u65f6\u95f4\u6b65\u751f\u6210\u3002',
  maxValue: '\u6700\u5927\u503c',
  period: '\u3002',
  chartLabel: 'Plant growth metric chart. Move to inspect; click to seek.',
}

function formatMetric(value: number) {
  return value.toLocaleString('zh-CN', { maximumFractionDigits: precision.value, minimumFractionDigits: precision.value })
}

function pointAt(event: MouseEvent) {
  const bounds = (event.currentTarget as SVGSVGElement).getBoundingClientRect()
  const progress = Math.max(0, Math.min(1, (event.clientX - bounds.left) / Math.max(1, bounds.width)))
  return findNearest(props.history, progress * props.maxAge)
}

function preview(event: MouseEvent) {
  hover.value = pointAt(event)
}

function seek(event: MouseEvent) {
  const point = pointAt(event)
  if (!point) return
  hover.value = point
  emit('seek', point.age)
}
</script>

<template>
  <section class="card chart">
    <header>
      <span>04</span><h2>{{ ui.title }}</h2>
      <nav><button v-for="(_, key) in props.metricNames" :key="key" :class="{ active: props.metric === key }" @click="emit('update:metric', key as Metric)">{{ props.metricNames[key as Metric] }}</button></nav>
    </header>
    <div class="chart-body">
      <div class="plot">
        <svg viewBox="0 0 1000 240" preserveAspectRatio="none" tabindex="0" :aria-label="ui.chartLabel" @mousemove="preview" @mouseleave="hover = null" @click="seek">
          <defs><linearGradient id="fill" x1="0" y1="0" x2="0" y2="1"><stop stop-color="#86db9b" stop-opacity=".38"/><stop offset="1" stop-color="#86db9b" stop-opacity="0"/></linearGradient></defs>
          <path class="grid" d="M0 30H1000M0 80H1000M0 130H1000M0 180H1000M0 228H1000"/>
          <path class="area" :d="area"/><path class="line" :d="path"/>
          <line v-for="stage in props.stages" :key="stage.key" class="stage-cursor" :x1="stage.age / props.maxAge * 1000" :x2="stage.age / props.maxAge * 1000" y1="18" y2="228"/>
          <line class="cursor" :x1="props.age / props.maxAge * 1000" :x2="props.age / props.maxAge * 1000" y1="18" y2="228"/><circle class="current-point" :cx="props.age / props.maxAge * 1000" :cy="cursorY" r="6"/>
        </svg>
        <div v-if="hover" class="chart-tooltip" :style="{ left: `${hoverProgress}%`, top: `${hoverY}px` }"><b>{{ hover.age.toFixed(2) }} &#24180;</b><span>{{ hover.lifeStage }}</span><strong>{{ formatMetric(hover[props.metric]) }}{{ unit }}</strong></div>
        <div class="y-axis"><span v-for="value in ticks" :key="value">{{ formatMetric(value) }}{{ unit }}</span></div>
        <div class="axis"><span>0 &#24180;</span><span>{{ (props.maxAge / 2).toFixed(1) }} &#24180;</span><span>{{ props.maxAge.toFixed(0) }} &#24180;</span></div>
      </div>
      <aside><span>{{ ui.currentMetric }}</span><strong>{{ props.currentValue.toFixed(isCountMetric ? 0 : 2) }}<small>{{ currentUnit }}</small></strong><p>{{ props.metricNames[props.metric] }}{{ ui.curveFrom }} {{ props.history.length }} {{ ui.timeSteps }}{{ ui.maxValue }} {{ chartMax.toFixed(1) }}{{ unit }}{{ ui.period }}</p></aside>
    </div>
  </section>
</template>
