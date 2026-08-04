const endpoint = process.env.PLANTSIM_WS_URL || 'ws://127.0.0.1:4317'
const socket = new WebSocket(endpoint)
let completed = false

const timeout = setTimeout(() => {
  console.error(`WebSocket timeout: ${endpoint}`)
  process.exit(1)
}, 5000)

socket.addEventListener('open', () => {
  console.log('CLIENT_CONNECTED')
  socket.send(JSON.stringify({ type: 'adjust_light', value: 0.9 }))
})

socket.addEventListener('message', (event) => {
  console.log(event.data)
  completed = true
  clearTimeout(timeout)
  socket.close()
})

socket.addEventListener('error', () => {
  if (completed) return
  clearTimeout(timeout)
  console.error(`WebSocket error: ${endpoint}`)
  process.exit(1)
})

socket.addEventListener('close', () => process.exit(completed ? 0 : 1))
