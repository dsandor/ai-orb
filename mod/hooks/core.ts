// Pure state reducers and the wire payload (docs/protocol.md), kept free of `$` so tests call them directly.
// prettyModel, describeInput, redact and limitLabel are adapted from Flightdeck (MIT, Stephen Casella).

import type { OrbAgent, OrbState } from '../types'

export const SCHEMA = 1
const MAX_AGENTS_KEPT = 12
const MAX_LOG = 4
/** An ask this old with no answer is a person being asked, even if no Notification arrived. */
export const ASK_GRACE_MS = 2500

export const DEFAULT_STATE: OrbState = {
  schema: SCHEMA,
  project: '',
  model: '',
  effort: '',
  mode: 'default',
  ttftMs: null,
  ctxPct: null,
  ctxTokens: null,
  ctxWindow: 0,
  compactions: 0,
  isCompacting: false,
  cost: null,
  limits: [],
  isRunning: false,
  turnStartedAt: 0,
  turnCostAtStart: null,
  turnSteps: 0,
  turnEdits: 0,
  turnErrors: 0,
  hasError: false,
  inflight: [],
  asks: [],
  promptedAt: null,
  tool: '',
  detail: '',
  gate: { allowed: 0, asked: 0, denied: 0 },
  agents: [],
  last: null,
  log: [],
}

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null && !Array.isArray(v)

/** A stored value merged over the defaults, so an older or partial shape still reads. */
export const normalize = (v: unknown): OrbState => {
  if (!isObject(v) || v.schema !== SCHEMA) return DEFAULT_STATE
  const s = { ...DEFAULT_STATE, ...v } as OrbState
  return {
    ...s,
    limits: Array.isArray(s.limits) ? s.limits : [],
    inflight: Array.isArray(s.inflight) ? s.inflight : [],
    asks: Array.isArray(s.asks) ? s.asks : [],
    agents: Array.isArray(s.agents) ? s.agents : [],
    log: Array.isArray(s.log) ? s.log : [],
    gate: { ...DEFAULT_STATE.gate, ...(isObject(s.gate) ? s.gate : {}) },
  }
}

// ---------------------------------------------------------------- text

export const shorten = (s: string, n: number) => {
  const one = s.replace(/\s+/g, ' ').trim()
  return one.length > n ? `${one.slice(0, Math.max(0, n - 1)).trimEnd()}…` : one
}

/** Display fonts on the device are ASCII: fold what they cannot draw. */
export const ascii = (s: string) =>
  s
    .replace(/[…]/g, '...')
    .replace(/[→]/g, '>')
    .replace(/[•]/g, '*')
    .replace(/[^\x20-\x7e]/g, '')

export const fit = (s: string, n: number) => {
  const a = ascii(s.replace(/\s+/g, ' ').trim())
  return a.length > n ? `${a.slice(0, Math.max(0, n - 3)).trimEnd()}...` : a
}

export const projectOf = (cwd: string) => cwd.split(/[\\/]/).filter(Boolean).pop() ?? ''

export const prettyModel = (id: string) => {
  if (!id) return ''
  const cap = (f: string) => f.charAt(0).toUpperCase() + f.slice(1)
  const big = /\[1m\]|-1m\b/i.test(id) ? ' 1M' : ''
  const now = /claude-([a-z]+)-(\d+)(?:-(\d{1,2}))?(?:-\d{8})?(?![\d])/i.exec(id)
  if (now?.[1] && !/^\d/.test(now[1])) return `${cap(now[1].toLowerCase())} ${now[2]}${now[3] ? `.${now[3]}` : ''}${big}`
  const old = /claude-(\d+)(?:-(\d))?-([a-z]+)/i.exec(id)
  if (old?.[3]) return `${cap(old[3].toLowerCase())} ${old[1]}${old[2] ? `.${old[2]}` : ''}${big}`
  return shorten(id, 22)
}

export const limitLabel = (kind: string) =>
  kind
    .replace(/five[_ -]?hours?/i, '5h')
    .replace(/seven[_ -]?days?/i, '7d')
    .replace(/[_-]+/g, ' ')
    .trim()

export const limitOf = (r: { kind: string; percentUsed: number; resetsAt?: string }) => {
  const at = r.resetsAt ? Date.parse(r.resetsAt) : NaN
  return { kind: limitLabel(r.kind), pct: r.percentUsed, resetsAt: Number.isFinite(at) ? at : null }
}

const SECRETS: [RegExp, string][] = [
  [/(authorization\s*[:=]\s*)(bearer\s+|basic\s+)?\S+/gi, '$1$2***'],
  [/\b(bearer)\s+[A-Za-z0-9._~+/-]{8,}=*/gi, '$1 ***'],
  [/\b(sk|pk|rk|ghp|gho|ghs|github_pat|xox[abprs])[-_][A-Za-z0-9_-]{8,}/g, '***'],
  [/((?:api[_-]?key|access[_-]?token|token|secret|password|passwd|pwd)\s*[=:]\s*)("[^"]*"|'[^']*'|\S+)/gi, '$1***'],
  [/(--(?:token|password|api-key|secret)[= ])\S+/gi, '$1***'],
  [/(\b[A-Z][A-Z0-9_]*(?:KEY|TOKEN|SECRET|PASSWORD)=)\S+/g, '$1***'],
  [/(\b[a-z][a-z0-9+.-]*:\/\/[^\s/:@]+:)[^\s@]+@/gi, '$1***@'],
]

/** Credentials masked before anything leaves the machine for the display. */
export const redact = (s: string) => SECRETS.reduce((t, [re, to]) => t.replace(re, to), s)

/** What a call is about, without the tool name: its command, path, pattern, url or description. */
export const detailOf = (input: unknown) => {
  const i = isObject(input) ? input : {}
  const path = typeof i.file_path === 'string' ? i.file_path.split(/[\\/]/).slice(-2).join('/') : ''
  const what =
    typeof i.command === 'string'
      ? i.command
      : path ||
        (typeof i.pattern === 'string'
          ? i.pattern
          : typeof i.url === 'string'
            ? i.url
            : typeof i.description === 'string'
              ? i.description
              : '')
  return redact(what)
}

export const EDIT_TOOLS = new Set(['Edit', 'Write', 'NotebookEdit'])

const pushLog = (log: string[], line: string) => [...log, fit(line, 32)].slice(-MAX_LOG)

// ---------------------------------------------------------------- reducers

export const onTurnStart = (s: OrbState, now: number, cost: number | null): OrbState => ({
  ...s,
  isRunning: true,
  turnStartedAt: now,
  turnCostAtStart: cost,
  turnSteps: 0,
  turnEdits: 0,
  turnErrors: 0,
  hasError: false,
  inflight: [],
  tool: '',
  detail: '',
})

export const onStep = (s: OrbState, model: string, effort: string | undefined): OrbState => ({
  ...s,
  model: prettyModel(model) || s.model,
  effort: effort ?? s.effort,
  turnSteps: s.turnSteps + 1,
})

export const onToolStart = (s: OrbState, id: string, tool: string, detail: string): OrbState => ({
  ...s,
  inflight: [...s.inflight.filter(c => c.id !== id), { id, tool, detail }].slice(-8),
  tool,
  detail,
})

export const onCheck = (
  s: OrbState,
  c: { id: string; tool: string; detail: string; decision: string; at: number },
): OrbState => {
  if (c.decision === 'deny') {
    return { ...s, gate: { ...s.gate, denied: s.gate.denied + 1 }, log: pushLog(s.log, `denied ${c.tool}`) }
  }
  if (c.decision === 'ask') {
    return {
      ...s,
      gate: { ...s.gate, asked: s.gate.asked + 1 },
      asks: [...s.asks.filter(a => a.id !== c.id), { id: c.id, tool: c.tool, detail: c.detail, at: c.at }].slice(-8),
    }
  }
  return { ...s, gate: { ...s.gate, allowed: s.gate.allowed + 1 } }
}

export const onToolEnd = (
  s: OrbState,
  c: { id: string; tool: string; detail: string; isMain: boolean; didRun: boolean; hasFailed: boolean },
): OrbState => {
  const wasAsked = s.asks.some(a => a.id === c.id)
  const asks = s.asks.filter(a => a.id !== c.id)
  const inflight = s.inflight.filter(x => x.id !== c.id)
  const top = inflight[inflight.length - 1]
  let next: OrbState = {
    ...s,
    asks,
    inflight,
    promptedAt: asks.length === 0 ? null : s.promptedAt,
    tool: c.isMain ? (top?.tool ?? c.tool) : s.tool,
    detail: c.isMain ? (top?.detail ?? c.detail) : s.detail,
  }
  if (!c.didRun && wasAsked) next = { ...next, gate: { ...next.gate, denied: next.gate.denied + 1 }, log: pushLog(next.log, `denied ${c.tool}`) }
  if (c.isMain && c.didRun) {
    next = { ...next, hasError: c.hasFailed }
    if (c.hasFailed) next = { ...next, turnErrors: next.turnErrors + 1, log: pushLog(next.log, `x ${c.tool} ${c.detail}`) }
    else if (EDIT_TOOLS.has(c.tool)) next = { ...next, turnEdits: next.turnEdits + 1, log: pushLog(next.log, `edit ${c.detail}`) }
    else if (c.tool === 'Bash') next = { ...next, log: pushLog(next.log, `$ ${c.detail}`) }
  }
  return next
}

export const onSpawn = (s: OrbState, a: { id: string; name: string; task: string; now: number }): OrbState => {
  const known = s.agents.find(x => x.id === a.id)
  const card: OrbAgent = known
    ? { ...known, status: 'running', endedAt: null }
    : { id: a.id, name: a.name, task: a.task, status: 'running', ctx: 0, steps: 0, spawnedAt: a.now, endedAt: null }
  const others = s.agents.filter(x => x.id !== a.id)
  return {
    ...s,
    agents: [...others, card].slice(-MAX_AGENTS_KEPT),
    log: known ? s.log : pushLog(s.log, `spawn ${a.name}`),
  }
}

export const onAgentStep = (s: OrbState, id: string, ctx: number | null): OrbState => ({
  ...s,
  agents: s.agents.map(a =>
    a.id === id ? { ...a, status: 'running', endedAt: null, steps: a.steps + 1, ctx: ctx ?? a.ctx } : a,
  ),
})

export const onAgentEnd = (s: OrbState, id: string, reason: string, now: number): OrbState => {
  const card = s.agents.find(a => a.id === id)
  if (!card) return s
  const status = reason === 'answer' ? 'done' : 'error'
  return {
    ...s,
    agents: s.agents.map(a => (a.id === id ? { ...a, status, endedAt: now } : a)),
    log: pushLog(s.log, `${status === 'done' ? 'done' : 'fail'} ${card.name}`),
  }
}

export const onTurnEnd = (s: OrbState, durationMs: number, cost: number | null): OrbState => ({
  ...s,
  isRunning: false,
  hasError: false,
  inflight: [],
  asks: [],
  promptedAt: null,
  last: {
    durationMs,
    costDelta: cost !== null && s.turnCostAtStart !== null ? Math.max(0, cost - s.turnCostAtStart) : null,
    edits: s.turnEdits,
    errors: s.turnErrors,
  },
})

/** Context input of one request: uncached + cache read + cache write. */
export const ctxOfUsage = (u: unknown): number | null => {
  if (!isObject(u)) return null
  const n = (k: string) => (typeof u[k] === 'number' ? (u[k] as number) : 0)
  const total = n('input_tokens') + n('cache_read_input_tokens') + n('cache_creation_input_tokens')
  return total > 0 ? total : null
}

// ---------------------------------------------------------------- the wire

export type Wire = ReturnType<typeof toWire>

export const stateOf = (s: OrbState, now: number) => {
  const isWaiting = s.asks.length > 0 && (s.promptedAt !== null || s.asks.some(a => now - a.at >= ASK_GRACE_MS))
  if (isWaiting) return 'waiting'
  if (!s.isRunning) return 'idle'
  if (s.isCompacting) return 'compacting'
  if (s.hasError) return 'error'
  if (s.inflight.length > 0) return 'tool'
  return 'thinking'
}

/** The snapshot protocol.md describes. */
export const toWire = (s: OrbState, sid: string, now: number) => {
  const state = stateOf(s, now)
  const ask = state === 'waiting' ? s.asks[0] : undefined
  const running = s.agents.filter(a => a.status === 'running')
  const list = [...running, ...s.agents.filter(a => a.status !== 'running').reverse()].slice(0, 4)
  const round = (n: number | null, d: number) => (n === null ? null : Math.round(n * 10 ** d) / 10 ** d)
  return {
    v: 1 as const,
    sid,
    project: fit(s.project, 24),
    state,
    tool: fit(ask?.tool ?? s.tool, 24),
    detail: fit(ask?.detail ?? s.detail, 40),
    model: fit(s.model, 16),
    effort: fit(s.effort, 8),
    mode: fit(s.mode, 18),
    ctx: { pct: round(s.ctxPct, 1), tokens: s.ctxTokens, window: s.ctxWindow, compactions: s.compactions },
    cost: round(s.cost, 3),
    limits: s.limits.slice(0, 2).map(l => ({
      kind: fit(l.kind, 8),
      pct: Math.round(l.pct),
      resetInMs: l.resetsAt === null ? null : Math.max(0, l.resetsAt - now),
    })),
    turn: {
      running: s.isRunning,
      elapsedMs: s.isRunning ? Math.max(0, now - s.turnStartedAt) : 0,
      steps: s.turnSteps,
      edits: s.turnEdits,
      errors: s.turnErrors,
    },
    last: s.last ? { ...s.last, costDelta: round(s.last.costDelta, 3) } : null,
    agents: {
      running: running.length,
      total: s.agents.length,
      list: list.map(a => ({
        name: fit(a.name, 16),
        task: fit(a.task, 28),
        status: a.status,
        ctx: a.ctx,
        steps: a.steps,
        elapsedMs: Math.max(0, (a.endedAt ?? now) - a.spawnedAt),
      })),
    },
    gate: { ...s.gate, pending: s.asks.length },
    ttftMs: s.ttftMs,
    log: s.log.slice(-MAX_LOG),
  }
}

/** What changes the picture, without the clocks: two pushes of the same picture are one. */
export const signature = (w: Wire) =>
  JSON.stringify({ ...w, turn: { ...w.turn, elapsedMs: 0 }, limits: w.limits.map(l => ({ ...l, resetInMs: 0 })), agents: { ...w.agents, list: w.agents.list.map(a => ({ ...a, elapsedMs: 0 })) } })

export const baseUrl = (url: string) => {
  const u = url.trim().replace(/\/+$/, '')
  if (!u) return ''
  return /^https?:\/\//i.test(u) ? u : `http://${u}`
}
