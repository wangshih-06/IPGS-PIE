<script setup lang="ts">
type Log = { time: string; text: string; tone: 'ok' | 'warn' | 'muted' }

const props = defineProps<{ logs: Log[] }>()
const emptyText = '\u7b49\u5f85\u751f\u957f\u4e8b\u4ef6\u2026'
const title = '\u56de\u653e\u4e8b\u4ef6'
</script>

<template>
  <section class="card events">
    <header><span>05</span><h2>{{ title }}</h2></header>
    <div v-if="props.logs.length" class="event-list"><div v-for="log in props.logs" :key="log.time + log.text"><time>{{ log.time }}</time><i :class="log.tone"></i><p>{{ log.text }}</p></div></div>
    <p v-else class="empty">{{ emptyText }}</p>
  </section>
</template>
