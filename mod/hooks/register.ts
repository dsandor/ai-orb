import { atom, read, update } from 'claude-code'
import type { EngineInterface, Register, Timer } from 'claude-code'

import type { OrbState } from '../types'
import {
  ASK_GRACE_MS,
  DEFAULT_STATE,
  DEFAULT_URL,
  DISCOVER_SCRIPT,
  baseUrl,
  ctxOfUsage,
  detailOf,
  deviceLabel,
  deviceUrl,
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
  parseDiscovery,
  pickDevice,
  projectOf,
  signature,
  toWire,
} from './core'
import type { OrbDevice } from './core'

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
  /** Where pushes go: the chosen orb, else the one orb found on the network, else `configUrl`. */
  url: '',
  /** `deviceUrl` from /config. */
  configUrl: '',
  /** This computer's pick among several orbs (`$.store`), while /config's URL is the one it was made under. */
  chosen: null as Choice | null,
  /** The single orb a scan found, used for this process when /config still holds the default. */
  foundUrl: '',
  didAutoScan: false,
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
  /** The URL the last successful push went to: the orb to tell when this session moves away. */
  sentTo: '',
}

// ---------------------------------------------------------------- which orb

type Choice = { url: string; label: string; configUrl: string }
const CHOICE_KEY = 'device'

const isChoice = (v: unknown): v is Choice =>
  typeof v === 'object' && v !== null && typeof (v as Choice).url === 'string' && typeof (v as Choice).configUrl === 'string'

/** Re-reads the stored pick (another session on this computer may have changed it) and follows it. */
async function loadChoice($: EngineInterface) {
  const v = await $.store.get(CHOICE_KEY).catch(() => undefined)
  // A pick made under another /config URL is stale: the person changed the setting since.
  link.chosen = isChoice(v) && v.configUrl === link.configUrl ? v : null
  await retarget($, link.chosen?.url || link.foundUrl || link.configUrl)
}

/** Points the pushes at `url`; the orb they leave is told this session ended there. */
async function retarget($: EngineInterface, url: string) {
  const old = link.url
  if (url === old) return
  link.url = url
  link.lastSig = ''
  link.retryAt = 0
  if (link.failures >= 3) $.ui.status(undefined)
  link.failures = 0
  if (old && old === link.sentTo && link.isEnabled) {
    link.sentTo = ''
    const sid = await $.session.id()
    const body = JSON.stringify({ v: 1, sid: sid.slice(0, 12) })
    void $.http.fetch(`${old}/api/end`, { method: 'POST', headers: headers(old), body }).catch(() => undefined)
  }
}

async function choose($: EngineInterface, pick: { url: string; label: string }) {
  const choice: Choice = { url: pick.url, label: pick.label, configUrl: link.configUrl }
  await $.store.set(CHOICE_KEY, choice)
  link.chosen = choice
  await retarget($, pick.url)
  schedule($, 50)
}

async function discover($: EngineInterface): Promise<{ devices: OrbDevice[]; error?: string }> {
  try {
    const r = await $.process.run(['/bin/sh', '-c', DISCOVER_SCRIPT], { timeoutMs: 10_000 })
    if (r.stdout.startsWith('#none')) return { devices: [], error: 'no mDNS browser here (needs dns-sd or avahi-browse)' }
    return { devices: parseDiscovery(r.stdout) }
  } catch (err) {
    return { devices: [], error: `could not scan: ${err instanceof Error ? err.message : String(err)}` }
  }
}

/** Puts the orbs to the person; null when the answer names none. Rejects when dismissed. */
async function askForDevice($: EngineInterface, devices: readonly OrbDevice[]) {
  const offered = devices.slice(0, 4)
  const labels = offered.map(d => `${deviceLabel(d)}${deviceUrl(d) === link.url ? ' - current' : ''}`)
  const more = devices.length > offered.length ? ` (${devices.length - offered.length} more: type a name or IP under Other)` : ''
  const answer = await $.ui.ask(`${devices.length} Desk Orbs are on your network. Which one should this computer's sessions show on?${more}`, {
    options: labels,
    header: 'Desk Orb',
  })
  const i = labels.indexOf(answer)
  const d = i >= 0 ? offered[i] : undefined
  return d ? { url: deviceUrl(d), label: deviceLabel(d) } : pickDevice(answer, devices)
}

/**
 * Once per process at session start, unless a pick is stored or /config names a URL of its own: one orb
 * on the network is used as found; several are put to the person, and the answer is kept.
 */
async function autoDiscover($: EngineInterface, isInteractive: boolean) {
  if (!link.isEnabled || link.chosen || link.didAutoScan || link.configUrl !== DEFAULT_URL) return
  link.didAutoScan = true
  const { devices } = await discover($)
  if (devices.length === 1) {
    link.foundUrl = deviceUrl(devices[0]!)
    if (!link.chosen) await retarget($, link.foundUrl)
    schedule($, 50)
    return
  }
  if (devices.length < 2 || !isInteractive) return
  await loadChoice($) // another session may have asked while this one scanned
  if (link.chosen) return
  try {
    const pick = await askForDevice($, devices)
    if (!pick) return
    await choose($, pick)
    $.ui.log(`Desk Orb: this computer now reports to ${pick.label}. /orb choose changes it.`)
  } catch {
    $.ui.log('Desk Orb: several orbs are on your network; run /orb choose to pick one.')
  }
}

const sourceOf = () =>
  link.chosen ? `${link.chosen.label}, chosen with /orb choose` : link.foundUrl && link.url === link.foundUrl ? 'the one orb found on the network' : 'from /config'

/**
 * The token goes only to an orb the person named: /config's URL or their pick. An orb that is merely
 * found on the network could be anything that advertises itself as one, so it gets no token.
 */
function headers(url: string): Record<string, string> {
  const isTrusted = url === link.configUrl || url === link.chosen?.url
  return { 'Content-Type': 'application/json', ...(link.token && isTrusted ? { 'X-Orb-Token': link.token } : {}) }
}

async function push($: EngineInterface, isHeartbeat = false) {
  if (!link.isEnabled || !link.url) return
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
    const r = await $.http.fetch(`${link.url}/api/state`, { method: 'POST', headers: headers(link.url), body: JSON.stringify(wire) })
    if (!r.ok) {
      const isFound = r.status === 401 && link.url === link.foundUrl && !link.chosen
      throw new Error(isFound ? 'needs a token: run /orb choose to trust this orb (401)' : r.status === 401 ? 'token refused (401)' : `HTTP ${r.status}`)
    }
    if (link.failures >= 3) $.ui.status(undefined)
    link.failures = 0
    link.retryAt = 0
    link.lastSig = sig
    link.lastSentAt = now
    link.sentTo = link.url
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
  link.heartbeat = link.isEnabled
    ? $.clock.every(HEARTBEAT_MS, () => void loadChoice($).then(() => push($, true)).catch(() => undefined))
    : null
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
  link.configUrl = baseUrl(typeof options.deviceUrl === 'string' ? options.deviceUrl : '')
  link.url = link.chosen?.url || link.foundUrl || link.configUrl
  link.token = typeof options.token === 'string' ? options.token.trim() : ''
  link.isEnabled = options.enabled !== false

  on('session.start', async ($, e, next) => {
    await $.command.register({
      name: 'orb',
      description: 'Desk Orb display: show its connection, or send a test push',
      argumentHint: '[status|test|scan|choose|use <name, IP or URL>|reset]',
    })
    await loadChoice($)
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
    void autoDiscover($, e.isInteractive).catch(() => undefined)
    return next(e)
  })

  on('session.end', async ($, e, next) => {
    if (link.isEnabled && link.url) {
      const body = JSON.stringify({ v: 1, sid: e.sessionId.slice(0, 12) })
      await $.http.fetch(`${link.url}/api/end`, { method: 'POST', headers: headers(link.url), body }).catch(() => undefined)
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
    const args = e.args.trim()
    const verb = args.split(/\s+/)[0] || 'status'
    const rest = args.slice(verb.length).trim()
    if (!link.isEnabled) return { text: 'Desk Orb is off: turn it on in /config.' }
    await loadChoice($)

    if (verb === 'scan' || verb === 'choose') {
      const { devices, error } = await discover($)
      if (devices.length === 0) {
        return { text: `No Desk Orbs found${error ? ` (${error})` : ''}. Pushing to ${link.url || 'nothing'}; /orb use <IP> sets one by hand.` }
      }
      if (verb === 'scan') {
        const rows = devices.map(d => `${deviceUrl(d) === link.url ? '*' : ' '} ${deviceLabel(d)}  ${deviceUrl(d)}${d.fw ? `  fw ${d.fw}` : ''}`)
        return { text: `Desk Orbs on your network (* = this computer's):\n${rows.join('\n')}\n/orb choose picks one.` }
      }
      let pick: { url: string; label: string } | null
      if (devices.length === 1) pick = { url: deviceUrl(devices[0]!), label: deviceLabel(devices[0]!) }
      else {
        try {
          pick = await askForDevice($, devices)
        } catch {
          return { text: `Kept ${link.url}.` }
        }
      }
      if (!pick) return { text: `That names no orb found. Kept ${link.url}.` }
      await choose($, pick)
      return { text: `Desk Orb -> ${pick.url} (${pick.label}).${devices.length === 1 ? ' It is the only orb found.' : ''}` }
    }

    if (verb === 'use') {
      const pick = pickDevice(rest, [])
      if (!pick) return { text: 'Usage: /orb use <name.local, IP or URL>' }
      await choose($, pick)
      return { text: `Desk Orb -> ${pick.url}` }
    }

    if (verb === 'reset') {
      await $.store.set(CHOICE_KEY, null)
      link.chosen = null
      await retarget($, link.foundUrl || link.configUrl)
      schedule($, 50)
      return { text: `Forgot the chosen orb. Desk Orb -> ${link.url} (${sourceOf()}).` }
    }

    if (!link.url) return { text: 'Desk Orb has no device: run /orb choose, or set a Device URL in /config.' }
    if (verb === 'test') {
      link.lastSig = ''
      link.retryAt = 0
      await push($, true)
      return { text: `Desk Orb -> ${link.url}: ${link.lastResult}` }
    }
    const ago = link.lastSentAt ? `${Math.round(((await $.clock.now()) - link.lastSentAt) / 1000)}s ago` : 'never'
    return {
      text: `Desk Orb -> ${link.url} (${sourceOf()})\nlast push: ${link.lastResult} (${ago})${link.failures ? `, ${link.failures} failures in a row` : ''}`,
    }
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
