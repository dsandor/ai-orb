import { atom, read, update } from 'claude-code'
import type { EngineInterface, Register, Timer } from 'claude-code'

import type { OrbState } from '../types'
import {
  ASK_GRACE_MS,
  DEFAULT_STATE,
  baseUrl,
  ctxOfUsage,
  detailOf,
  limitOf,
  normalize,
  onAgentEnd,
  onAgentStep,
  onCheck,
  onSpawn,
  onStep,
  onToolEnd,
  onToolStart,
  onTurnEnd,
  onTurnStart,
  projectOf,
  signature,
  toWire,
} from './core'

const orb = atom({ plugin: 'deskorb', key: 'orb' } as const, DEFAULT_STATE)

const DEBOUNCE_MS = 400
const HEARTBEAT_MS = 10_000
/** Notification types that mean a person is being asked something right now. */
const ASKING = new Set(['permission_prompt', 'elicitation_dialog'])

async function get($: EngineInterface): Promise<OrbState> {
  return normalize(await read($, orb))
}

async function costNow($: EngineInterface): Promise<number | null> {
  const u = await $.session.usage().catch(() => null)
  return u?.cost?.usd ?? null
}

// The push loop: one request in flight at a time, the latest picture wins, and an unreachable
// display backs off instead of being asked on every event. Module variables start over on reload.
const link = {
  url: '',
  token: '',
  isEnabled: false,
  timer: null as Timer | null,
  heartbeat: null as Timer | null,
  isSending: false,
  isDirty: false,
  lastSig: '',
  lastSentAt: 0,
  failures: 0,
  retryAt: 0,
  lastResult: 'nothing sent yet',
}

function headers(): Record<string, string> {
  return { 'Content-Type': 'application/json', ...(link.token ? { 'X-Orb-Token': link.token } : {}) }
}

async function push($: EngineInterface, isHeartbeat = false) {
  if (!link.isEnabled) return
  if (link.isSending) {
    link.isDirty = true
    return
  }
  const now = await $.clock.now()
  if (now < link.retryAt && !isHeartbeat) return
  const sid = await $.session.id()
  const wire = toWire(await get($), sid.slice(0, 12), now)
  const sig = signature(wire)
  if (!isHeartbeat && sig === link.lastSig) return
  link.isSending = true
  try {
    const r = await $.http.fetch(`${link.url}/api/state`, { method: 'POST', headers: headers(), body: JSON.stringify(wire) })
    if (!r.ok) throw new Error(r.status === 401 ? 'token refused (401)' : `HTTP ${r.status}`)
    if (link.failures >= 3) $.ui.status(undefined)
    link.failures = 0
    link.retryAt = 0
    link.lastSig = sig
    link.lastSentAt = now
    link.lastResult = `ok, ${wire.state}`
  } catch (err) {
    link.failures += 1
    link.retryAt = now + Math.min(60_000, 3_000 * link.failures)
    link.lastResult = `failed: ${err instanceof Error ? err.message : String(err)}`
    if (link.failures === 3) $.ui.status('orb offline')
  } finally {
    link.isSending = false
  }
  if (link.isDirty) {
    link.isDirty = false
    schedule($)
  }
}

function schedule($: EngineInterface, ms = DEBOUNCE_MS) {
  if (!link.isEnabled || link.timer) return
  link.timer = $.clock.after(ms, () => {
    link.timer = null
    void push($).catch(() => undefined)
  })
}

function startHeartbeat($: EngineInterface) {
  link.heartbeat?.cancel()
  link.heartbeat = link.isEnabled ? $.clock.every(HEARTBEAT_MS, () => void push($, true).catch(() => undefined)) : null
}

function recheckLater($: EngineInterface, ms: number) {
  $.clock.after(ms, () => void push($).catch(() => undefined))
}

/** Every hook here only watches: a failed write must never fail the event it rides on. */
async function mutate($: EngineInterface, fn: (s: OrbState) => OrbState) {
  try {
    await update($, orb, x => fn(normalize(x)))
    schedule($)
  } catch {
    // the next event writes a whole picture again
  }
}

export const register: Register = (on, options) => {
  link.url = baseUrl(typeof options.deviceUrl === 'string' ? options.deviceUrl : '')
  link.token = typeof options.token === 'string' ? options.token.trim() : ''
  link.isEnabled = options.enabled !== false && link.url !== ''

  on('session.start', async ($, e, next) => {
    await $.command.register({
      name: 'orb',
      description: 'Desk Orb display: show its connection, or send a test push',
      argumentHint: '[status|test]',
    })
    const u = await $.session.usage().catch(() => null)
    await update($, orb, x => {
      const s = normalize(x)
      return {
        ...s,
        project: projectOf(e.cwd),
        ...(u
          ? {
              ctxPct: u.context.percent ?? null,
              ctxTokens: u.context.tokens ?? null,
              ctxWindow: u.context.window,
              cost: u.cost?.usd ?? null,
              limits: u.rateLimits.map(limitOf),
            }
          : {}),
      }
    })
    startHeartbeat($)
    schedule($, 50)
    return next(e)
  })

  on('session.end', async ($, e, next) => {
    if (link.isEnabled) {
      const body = JSON.stringify({ v: 1, sid: e.sessionId.slice(0, 12) })
      await $.http.fetch(`${link.url}/api/end`, { method: 'POST', headers: headers(), body }).catch(() => undefined)
    }
    if (e.reason === 'clear') {
      link.lastSig = ''
      await update($, orb, x => {
        const s = normalize(x)
        return { ...DEFAULT_STATE, project: s.project, model: s.model, effort: s.effort, mode: s.mode, cost: s.cost, limits: s.limits }
      })
      schedule($)
    } else {
      link.heartbeat?.cancel()
      link.heartbeat = null
    }
    return next(e)
  })

  on('command.run', { command: 'orb' }, async ($, e) => {
    const verb = e.args.trim().split(/\s+/)[0] || 'status'
    if (!link.isEnabled) return { text: 'Desk Orb is off: set a Device URL and turn it on in /config.' }
    if (verb === 'test') {
      link.lastSig = ''
      link.retryAt = 0
      await push($, true)
      return { text: `Desk Orb -> ${link.url}: ${link.lastResult}` }
    }
    const ago = link.lastSentAt ? `${Math.round(((await $.clock.now()) - link.lastSentAt) / 1000)}s ago` : 'never'
    return { text: `Desk Orb -> ${link.url}\nlast push: ${link.lastResult} (${ago})${link.failures ? `, ${link.failures} failures in a row` : ''}` }
  })

  on('classic.UserPromptSubmit', async ($, e, next) => {
    if (e.permission_mode) await mutate($, s => (s.mode === e.permission_mode ? s : { ...s, mode: e.permission_mode ?? s.mode }))
    return next(e)
  })

  on('classic.Notification', async ($, e, next) => {
    if (ASKING.has(e.notification_type)) {
      const now = await $.clock.now()
      await mutate($, s => (s.asks.length > 0 ? { ...s, promptedAt: now } : s))
    }
    return next(e)
  })

  on('turn.start', async ($, e, next) => {
    if (e.agentId !== undefined) return next(e)
    const [now, cost] = await Promise.all([$.clock.now(), costNow($)])
    await mutate($, s => onTurnStart(s, now, cost))
    return next(e)
  })

  on('turn.step', async function* ($, e, next) {
    if (e.agentId) {
      const id = e.agentId
      const result = yield* next(e)
      await mutate($, s => onAgentStep(s, id, ctxOfUsage(result.usage)))
      return result
    }
    await mutate($, s => onStep(s, e.model, e.effort === undefined ? undefined : String(e.effort)))
    const sentAt = await $.clock.now()
    let firstAt: number | null = null
    const stream = next(e)
    for await (const c of stream) {
      if (firstAt === null && c.kind !== 'engine' && c.kind !== 'stop') firstAt = await $.clock.now()
      yield c
    }
    const result = await stream.result
    if (firstAt !== null) {
      const ttft = firstAt - sentAt
      await mutate($, s => ({ ...s, ttftMs: ttft }))
    }
    return result
  })

  on('session.measure', async ($, e, next) => {
    await mutate($, s => ({
      ...s,
      ctxPct: e.context.percent ?? null,
      ctxTokens: e.context.tokens ?? null,
      ctxWindow: e.context.window,
      cost: e.cost?.usd ?? s.cost,
      limits: e.rateLimits.map(limitOf),
    }))
    return next(e)
  })

  on('session.compact', async ($, e, next) => {
    const isMain = !e.agentId && e.trigger !== 'precompute'
    if (isMain) await mutate($, s => ({ ...s, isCompacting: true }))
    try {
      return await next(e)
    } finally {
      if (isMain) await mutate($, s => ({ ...s, isCompacting: false, compactions: s.compactions + 1 }))
    }
  })

  on('tool.check', async ($, e, next) => {
    const verdict = await next(e)
    if (e.tool_use_id) {
      const id = e.tool_use_id
      const now = await $.clock.now()
      await mutate($, s => onCheck(s, { id, tool: e.tool, detail: detailOf(e.input), decision: verdict.decision, at: now }))
      // No event marks the moment an unanswered ask becomes a person's; look again after the grace.
      if (verdict.decision === 'ask') recheckLater($, ASK_GRACE_MS + 100)
    }
    return verdict
  })

  on('tool.call', async ($, e, next) => {
    const isMain = !e.agentId
    const detail = detailOf(e)
    if (isMain) await mutate($, s => onToolStart(s, e.tool_use_id, e.tool, detail))
    let didRun = false
    let hasFailed = false
    try {
      const ran = await next(e)
      didRun = ran.deny === undefined
      hasFailed = didRun && ran.isError === true
      return ran
    } finally {
      await mutate($, s => onToolEnd(s, { id: e.tool_use_id, tool: e.tool, detail, isMain, didRun, hasFailed }))
    }
  })

  on('agent.spawn', async ($, e, next) => {
    const started = await next(e)
    if (started.deny !== undefined || !started.agentId) return started
    const id = started.agentId
    const now = await $.clock.now()
    await mutate($, s => onSpawn(s, { id, name: e.name ?? e.subagentType.split(':').pop() ?? 'agent', task: e.description, now }))
    return started
  })

  on('turn.complete', async ($, e, next) => {
    const done = await next(e)
    if (e.agentId) {
      const id = e.agentId
      const now = await $.clock.now()
      await mutate($, s => onAgentEnd(s, id, e.reason, now))
    } else {
      const cost = await costNow($)
      await mutate($, s => onTurnEnd(s, e.durationMs, cost))
    }
    return done
  })
}
