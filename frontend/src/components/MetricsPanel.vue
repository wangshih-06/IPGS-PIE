<script setup lang="ts">
const props = defineProps<{
  recordedFrameCount: number
  recordedEndAge: number
  speed: number
  nodeCount: number
}>()

const emit = defineEmits<{
  speedChange: [value: number]
  export: [kind: 'json' | 'csv']
}>()

const ui = {
  title: '\u8bb0\u5f55\u6458\u8981',
  frequency: '\u8bb0\u5f55\u9891\u7387',
  everyStep: '\u6bcf\u4e2a\u65f6\u95f4\u6b65',
  duration: '\u5df2\u8bb0\u5f55\u65f6\u957f',
  speed: '\u56de\u653e\u901f\u5ea6',
  activeNodes: '\u6d3b\u8dc3\u8282\u70b9',
  exportJson: '\u5bfc\u51fa JSON',
  exportCsv: '\u5bfc\u51fa CSV',
  hint: '\u5f15\u64ce\u7aef\u4fdd\u5b58\u5b8c\u6574\u690d\u7269\u72b6\u6001\u5feb\u7167\uff1b\u6b64\u5904\u53ef\u4e0b\u8f7d\u7528\u4e8e\u66f2\u7ebf\u5206\u6790\u7684\u6307\u6807\u6570\u636e\u3002',
}

function updateSpeed(event: Event) {
  emit('speedChange', Number((event.target as HTMLInputElement).value))
}
</script>

<template>
  <aside class="card summary">
    <header><span>02</span><h2>{{ ui.title }}</h2><b>{{ props.recordedFrameCount }} FRAMES</b></header>
    <dl>
      <div><dt>{{ ui.frequency }}</dt><dd>{{ ui.everyStep }}</dd></div>
      <div><dt>{{ ui.duration }}</dt><dd>{{ props.recordedEndAge.toFixed(2) }} &#24180;</dd></div>
      <div><dt>{{ ui.speed }}</dt><dd>{{ props.speed.toFixed(1) }}&#215;</dd></div>
      <div><dt>{{ ui.activeNodes }}</dt><dd>{{ props.nodeCount }}</dd></div>
    </dl>
    <label class="speed">{{ ui.speed }} <b>{{ props.speed.toFixed(1) }}&#215;</b>
      <input type="range" min=".1" max="8" step=".1" :value="props.speed" @input="updateSpeed">
    </label>
    <div class="exports">
      <button class="primary" @click="emit('export', 'json')">{{ ui.exportJson }}</button>
      <button class="ghost" @click="emit('export', 'csv')">{{ ui.exportCsv }}</button>
    </div>
    <p class="hint">{{ ui.hint }}</p>
  </aside>
</template>
