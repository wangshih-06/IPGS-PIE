<script setup lang="ts">
import { stageTitle, type Stage } from '../lib/growthData'

const props = defineProps<{
  age: number
  maxAge: number
  progress: number
  playing: boolean
  pending: boolean
  stages: Stage[]
}>()

const emit = defineEmits<{
  toggle: []
  preview: [age: number]
  seek: [age: number]
  reset: []
  stage: [stage: Stage]
}>()

const ui = {
  title: '\u751f\u957f\u8fc7\u7a0b\u56de\u653e',
  pause: '\u6682\u505c\u56de\u653e',
  play: '\u5f00\u59cb\u56de\u653e',
  reset: '\u91cd\u7f6e\u8bb0\u5f55',
}

function inputAge(event: Event) {
  return Number((event.target as HTMLInputElement).value)
}
</script>

<template>
  <section class="card timeline">
    <header><span>03</span><h2>{{ ui.title }}</h2><b>T = {{ props.age.toFixed(2) }} / {{ props.maxAge.toFixed(0) }} &#24180;</b></header>
    <div class="timeline-row">
      <button class="play" :disabled="props.pending" :aria-label="props.playing ? ui.pause : ui.play" @click="emit('toggle')"><i :class="props.playing ? 'pause' : 'triangle'"></i></button>
      <div class="track">
        <input type="range" min="0" :max="props.maxAge" step=".01" :value="props.age" @input="emit('preview', inputAge($event))" @change="emit('seek', inputAge($event))">
        <i :style="{ width: `${props.progress}%` }"></i>
      </div>
      <button class="ghost" @click="emit('reset')">{{ ui.reset }}</button>
    </div>
    <div class="stages">
      <button v-for="stage in props.stages" :key="stage.key" :class="{ active: Math.abs(props.age - stage.age) < .35 }" @click="emit('stage', stage)">
        <i></i><span>{{ stageTitle(stage) }}</span><small>{{ stage.age }} &#24180;</small>
      </button>
    </div>
  </section>
</template>
