import { computed, ref } from 'vue'

export type EngineConnection = 'connecting' | 'connected' | 'reconnecting' | 'offline'
export type EngineLogTone = 'ok' | 'warn' | 'muted'

export interface EngineSocketOptions {
  url: string
  onMessage: (payload: Record<string, unknown>) => void
  onConnected?: () => void
  onDisconnected?: () => void
  onLog?: (message: string, tone?: EngineLogTone) => void
  protocolVersion?: number
  maxReconnectAttempts?: number
}

async function decodeEnginePayload(payload: unknown): Promise<Record<string, unknown>> {
  if (typeof payload === 'string') return JSON.parse(payload) as Record<string, unknown>
  if (!(payload instanceof Blob)) throw new Error('Unsupported WebSocket payload')

  const bytes = new Uint8Array(await payload.arrayBuffer())
  const marker = new TextDecoder().decode(bytes.slice(0, 4))
  if (marker !== 'PSZ1') throw new Error('Unknown binary WebSocket payload')
  if (typeof DecompressionStream === 'undefined') {
    throw new Error('Browser does not support compressed engine messages')
  }

  // qCompress prefixes its zlib stream with a four-byte uncompressed-size field.
  const zlibPayload = bytes.slice(8)
  const decompressed = await new Response(
    new Blob([zlibPayload]).stream().pipeThrough(new DecompressionStream('deflate')),
  ).arrayBuffer()
  return JSON.parse(new TextDecoder().decode(decompressed)) as Record<string, unknown>
}

export function useEngineSocket(options: EngineSocketOptions) {
  const socket = ref<WebSocket | null>(null)
  const connection = ref<EngineConnection>('connecting')
  const reconnectAttempt = ref(0)
  const reconnectDelay = ref(0)
  const isConnected = computed(() => connection.value === 'connected')
  const protocolVersion = options.protocolVersion ?? 1
  const maxReconnectAttempts = options.maxReconnectAttempts ?? 5

  let reconnectTimer = 0
  let allowReconnect = true

  function log(message: string, tone: EngineLogTone = 'muted') {
    options.onLog?.(message, tone)
  }

  function clearReconnectTimer() {
    if (reconnectTimer) window.clearTimeout(reconnectTimer)
    reconnectTimer = 0
  }

  function send(command: Record<string, unknown>) {
    try {
      if (socket.value?.readyState !== WebSocket.OPEN) return false
      socket.value.send(JSON.stringify({ ...command, protocolVersion }))
      return true
    } catch {
      return false
    }
  }

  function scheduleReconnect(reason: string) {
    if (!allowReconnect || reconnectTimer || isConnected.value) return
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
    reconnectTimer = window.setTimeout(() => {
      reconnectTimer = 0
      connect(false)
    }, reconnectDelay.value * 1000)
  }

  function connect(manual = true) {
    if (manual) {
      allowReconnect = true
      reconnectAttempt.value = 0
      reconnectDelay.value = 0
    }

    clearReconnectTimer()
    const previous = socket.value
    socket.value = null
    previous?.close()
    connection.value = manual ? 'connecting' : 'reconnecting'

    let ws: WebSocket
    try {
      ws = new WebSocket(options.url)
    } catch {
      scheduleReconnect('创建失败')
      return
    }

    socket.value = ws
    ws.binaryType = 'blob'
    ws.onopen = () => {
      if (socket.value !== ws) return
      connection.value = 'connected'
      reconnectAttempt.value = 0
      reconnectDelay.value = 0
      options.onConnected?.()
    }
    ws.onmessage = async (event) => {
      if (socket.value !== ws) return
      try {
        options.onMessage(await decodeEnginePayload(event.data))
      } catch {
        log('收到无法解析的引擎消息。', 'warn')
      }
    }
    ws.onerror = () => {
      // The close handler owns state transitions to avoid duplicate messages.
    }
    ws.onclose = () => {
      if (socket.value !== ws) return
      socket.value = null
      options.onDisconnected?.()
      scheduleReconnect('已关闭')
    }
  }

  function dispose() {
    allowReconnect = false
    clearReconnectTimer()
    const active = socket.value
    socket.value = null
    active?.close()
  }

  return { socket, connection, reconnectDelay, isConnected, send, connect, dispose }
}
