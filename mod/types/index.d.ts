export type OrbAgent = {
  id: string
  name: string
  task: string
  status: 'running' | 'done' | 'error'
  ctx: number
  steps: number
  spawnedAt: number
  endedAt: number | null
}

export type OrbAsk = { id: string; tool: string; detail: string; at: number }

export type OrbLimit = { kind: string; pct: number; resetsAt: number | null }

export type OrbReceipt = { durationMs: number; costDelta: number | null; edits: number; errors: number }

export type OrbState = {
  schema: number
  project: string
  model: string
  effort: string
  mode: string
  ttftMs: number | null
  ctxPct: number | null
  ctxTokens: number | null
  ctxWindow: number
  compactions: number
  isCompacting: boolean
  cost: number | null
  limits: OrbLimit[]
  /** Main-loop turn. */
  isRunning: boolean
  turnStartedAt: number
  turnCostAtStart: number | null
  turnSteps: number
  turnEdits: number
  turnErrors: number
  /** Last main-loop tool call that failed, while the turn runs. */
  hasError: boolean
  /** Main-loop tool calls in flight, keyed by tool_use_id. */
  inflight: { id: string; tool: string; detail: string }[]
  /** Permission asks not settled yet (main loop or subagents). */
  asks: OrbAsk[]
  /** Set by the classic Notification hook when Claude Code puts a question to the person. */
  promptedAt: number | null
  tool: string
  detail: string
  gate: { allowed: number; asked: number; denied: number }
  agents: OrbAgent[]
  last: OrbReceipt | null
  log: string[]
}

declare module 'claude-code' {
  interface PluginState {
    'deskorb': {
      orb: OrbState
    }
  }
}
