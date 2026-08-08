<script setup lang="ts">
import { computed } from 'vue'
import { stageTitle, type Stage } from '../lib/growthData'

const props = defineProps<{
  age: number
  maxAge: number
  progress: number
  playing: boolean
  pending: boolean
  atLatest: boolean
  stages: Stage[]
}>()

const emit = defineEmits<{
  toggle: []
  preview: [age: number]
  seek: [age: number]
  reset: []
  latest: []
  stage: [stage: Stage]
}>()

const ui = {
  title: '\u751f\u957f\u8fc7\u7a0b\u56de\u653e',
  pause: '\u6682\u505c\u56de\u653e',
  play: '\u5f00\u59cb\u56de\u653e',
  reset: '\u91cd\u7f6e\u8bb0\u5f55',
  latest: '\u56de\u5230\u6700\u65b0\u5e27',
}

const positionText = computed(() => `\u5f53\u524d\u56de\u653e\u4f4d\u7f6e\uff1a${props.age.toFixed(2)} \u5e74\uff0c\u603b\u65f6\u957f ${props.maxAge.toFixed(2)} \u5e74`)

function isActive(stage: Stage) {
  return Math.abs(props.age - stage.age) < 0.35
}

function inputAge(event: Event) {
  return Number((event.target as HTMLInputElement).value)
}
</script>

<template>
  <section class="card timeline">
    <header><span>03</span><h2>{{ ui.title }}</h2><b>T = {{ props.age.toFixed(2) }} / {{ props.maxAge.toFixed(0) }} &#24180;</b></header>
    <div class="timeline-row">
      <button type="button" class="play" :disabled="props.pending" :aria-label="props.playing ? ui.pause : ui.play" @click="emit('toggle')"><i :class="props.playing ? 'pause' : 'triangle'"></i></button>
      <div class="track">
        <input type="range" min="0" :max="props.maxAge" step=".01" :value="props.age" :aria-valuetext="positionText" @input="emit('preview', inputAge($event))" @change="emit('seek', inputAge($event))">
        <i :style="{ width: `${props.progress}%` }"></i>
      </div>
      <div class="timeline-actions"><button v-if="!props.atLatest" type="button" class="ghost" :disabled="props.pending" @click="emit('latest')">{{ ui.latest }}</button><button type="button" class="ghost" :disabled="props.pending" @click="emit('reset')">{{ ui.reset }}</button></div>
    </div>
    <div class="stages">
      <button v-for="stage in props.stages" :key="stage.key" type="button" :class="{ active: isActive(stage) }" :aria-pressed="isActive(stage)" :aria-current="isActive(stage) ? 'step' : undefined" :disabled="props.pending" @click="emit('stage', stage)">
        <i></i><span>{{ stageTitle(stage) }}</span><small>{{ stage.age }} &#24180;</small>
      </button>
    </div>
  </section>
</template>
