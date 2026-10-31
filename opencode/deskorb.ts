// Desk Orb for OpenCode (V1 plugin API, opencode 1.x): pushes each session's status to the orb,
// with the same reducers and wire format as the Claude Code mod (../mod/hooks/core.ts, docs/protocol.md).
//
// Install: symlink this file into ~/.config/opencode/plugins/ (see README.md). It imports ../mod, so
// keep the clone where it is. OpenCode runs every export of this file as a plugin: export nothing else.
//
// Which orb, first that applies:
//   1. plugin options { deviceUrl, token } (opencode.json "plugin": [["/abs/path/deskorb.ts", {...}]])
//   2. ORB_URL / ORB_TOKEN environment variables
//   3. ~/.config/deskorb/device.json, written by tools/orb-choose.py
//   4. an mDNS scan: one orb found is used; several are listed in a toast until one is chosen

import { homedir } from 'node:os'
import { join } from 'node:path'
import { readFileSync } from 'node:fs'

import type { OrbState } from '../mod/types'
import {
  DEFAULT_STATE,
  DISCOVER_SCRIPT,
  baseUrl,
  detailOf,
  deviceLabel,
  deviceUrl,
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
  projectOf,
  signature,
  toWire,
} from '../mod/hooks/core'

const DEBOUNCE_MS = 400
const HEARTBEAT_MS = 10_000
const CHOICE_FILE = join(homedir(), '.config', 'deskorb', 'device.json')

type Sess = { s: OrbState; costs: Map<string, number>; seen: Set<string>; lastSig: string; asks: Map<string, string> }

const readChoice = (): { url: string; token: string } | null => {
  try {
    const v = JSON.parse(readFileSync(CHOICE_FILE, 'utf8'))
    return typeof v?.url === 'string' && v.url ? { url: baseUrl(v.url), token: typeof v.token === 'string' ? v.token : '' } : null
  } catch {
    return null
  }
}

/** OpenCode's tool names are lower case and its inputs camelCase; core speaks Claude Code's. */
const toolName = (t: string) => (t ? t.charAt(0).toUpperCase() + t.slice(1) : t)
const detailOfInput = (i: any) => detailOf({ ...i, file_path: i?.filePath ?? i?.file_path })

export const DeskOrb = async ({ client, directory, worktree }: any, options?: Record<string, unknown>) => {
  const project = projectOf(directory || worktree || process.cwd())
  const fixedUrl = baseUrl(String(options?.deviceUrl ?? process.env.ORB_URL ?? ''))
  const fixedToken = String(options?.token ?? process.env.ORB_TOKEN ?? '')
  let url = fixedUrl
  let token = fixedToken
  let foundUrl = ''

  const sessions = new Map<string, Sess>()
  const parentOf = new Map<string, string>() // subagent session -> its root session
  let timer: ReturnType<typeof setTimeout> | null = null
  let failures = 0
  let retryAt = 0

  const toast = (message: string, variant = 'info') =>
    client?.tui?.showToast?.({ body: { title: 'Desk Orb', message, variant } })?.catch?.(() => undefined)

  /** Re-reads the choice file (tools/orb-choose.py may have changed it) unless options or env fix the URL. */
  const resolve = () => {
    if (fixedUrl) return
    const c = readChoice()
    const next = c?.url || foundUrl
    if (next !== url) {
      url = next
      failures = 0
      retryAt = 0
      for (const x of sessions.values()) x.lastSig = ''
    }
    token = c?.token || fixedToken
  }

  const post = async (path: string, body: unknown) => {
    const r = await fetch(`${url}${path}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', ...(token ? { 'X-Orb-Token': token } : {}) },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(3000),
    })
    if (!r.ok) throw new Error(`HTTP ${r.status}`)
  }

  const wireSid = (id: string) => `oc${id.slice(-10)}`

  const flush = async (isHeartbeat: boolean) => {
    if (!url) return
    const now = Date.now()
    if (now < retryAt && !isHeartbeat) return
    for (const [id, x] of sessions) {
      const wire = toWire(x.s, wireSid(id), now)
      const sig = signature(wire)
      if (!isHeartbeat && sig === x.lastSig) continue
      try {
        await post('/api/state', wire)
        x.lastSig = sig
        failures = 0
        retryAt = 0
      } catch {
        failures += 1
        retryAt = now + Math.min(60_000, 3_000 * failures)
        if (failures === 3) toast(`Can't reach the orb at ${url}`, 'warning')
        return
      }
    }
  }

  const schedule = () => {
    if (timer) return
    timer = setTimeout(() => {
      timer = null
      void flush(false)
    }, DEBOUNCE_MS)
  }

  const heartbeat = setInterval(() => {
    resolve()
    void flush(true)
  }, HEARTBEAT_MS)
  heartbeat.unref?.()

  const sess = (id: string): Sess => {
    let x = sessions.get(id)
    if (!x) {
      x = { s: { ...DEFAULT_STATE, project }, costs: new Map(), seen: new Set(), lastSig: '', asks: new Map() }
      sessions.set(id, x)
    }
    return x
  }

  const mutate = (id: string, fn: (s: OrbState, x: Sess) => OrbState) => {
    const x = sess(id)
    x.s = fn(x.s, x)
    schedule()
  }

  const costOf = (x: Sess) => (x.costs.size ? [...x.costs.values()].reduce((a, b) => a + b, 0) : null)

  // Which orb: scan when nothing names one.
  resolve()
  if (!url) {
    void (async () => {
      try {
        const p = (globalThis as any).Bun.spawn(['/bin/sh', '-c', DISCOVER_SCRIPT], { stdout: 'pipe', stderr: 'ignore' })
        const devices = parseDiscovery(await new Response(p.stdout).text())
        if (devices.length === 1) {
          foundUrl = deviceUrl(devices[0]!)
          resolve()
          schedule()
        } else if (devices.length > 1 && !url) {
          toast(
            `${devices.length} orbs found: ${devices.map(deviceLabel).join(', ')}. Pick one with: python3 tools/orb-choose.py (in the ai-orb clone)`,
            'warning',
          )
        }
      } catch {
        // no mDNS browser here: set ORB_URL or use tools/orb-choose.py --url
      }
    })()
  }

  return {
    event: async ({ event }: any) => {
      const p = event?.properties ?? {}
      const now = Date.now()
      switch (event?.type) {
        case 'session.created': {
          const info = p.info
          if (!info?.id) return
          if (info.parentID) {
            const root = parentOf.get(info.parentID) ?? info.parentID
            parentOf.set(info.id, root)
            mutate(root, s => onSpawn(s, { id: info.id, name: 'agent', task: String(info.title ?? ''), now }))
          } else sess(info.id)
          return
        }
        case 'session.updated': {
          const info = p.info
          if (!info?.id || parentOf.has(info.id)) return
          const isCompacting = Boolean(info.time?.compacting)
          if (sessions.has(info.id) && sess(info.id).s.isCompacting !== isCompacting) mutate(info.id, s => ({ ...s, isCompacting }))
          return
        }
        case 'session.compacted':
          if (p.sessionID && !parentOf.has(p.sessionID)) mutate(p.sessionID, s => ({ ...s, isCompacting: false, compactions: s.compactions + 1 }))
          return
        case 'session.status': {
          const id = p.sessionID
          const type = p.status?.type
          if (!id) return
          const root = parentOf.get(id)
          if (root) {
            if (type === 'idle') mutate(root, s => onAgentEnd(s, id, 'answer', now))
            return
          }
          const x = sess(id)
          if (type === 'busy' && !x.s.isRunning) mutate(id, (s, y) => onTurnStart(s, now, costOf(y) ?? 0))
          if (type === 'idle' && x.s.isRunning) mutate(id, (s, y) => onTurnEnd(s, now - s.turnStartedAt, costOf(y)))
          return
        }
        case 'session.error': {
          const id = p.sessionID
          if (!id) return
          const root = parentOf.get(id)
          if (root) mutate(root, s => onAgentEnd(s, id, 'error', now))
          else mutate(id, s => ({ ...s, hasError: s.isRunning, turnErrors: s.turnErrors + 1 }))
          return
        }
        case 'session.deleted': {
          const id = p.info?.id
          if (!id || !sessions.has(id)) return
          sessions.delete(id)
          if (url) void post('/api/end', { v: 1, sid: wireSid(id) }).catch(() => undefined)
          return
        }
        case 'message.updated': {
          const m = p.info
          if (m?.role !== 'assistant' || !m.sessionID) return
          const root = parentOf.get(m.sessionID)
          const t = m.tokens
          const ctx = t ? (t.input ?? 0) + (t.cache?.read ?? 0) + (t.cache?.write ?? 0) : 0
          if (root) {
            if (m.time?.completed) mutate(root, s => onAgentStep(s, m.sessionID, ctx || null))
            return
          }
          mutate(m.sessionID, (s, x) => {
            if (typeof m.cost === 'number') x.costs.set(m.id, m.cost)
            let next = s
            if (!x.seen.has(m.id)) {
              x.seen.add(m.id)
              next = onStep(next, String(m.modelID ?? ''), undefined)
            }
            return { ...next, cost: costOf(x), ...(ctx ? { ctxTokens: ctx } : {}) }
          })
          return
        }
        case 'message.part.updated': {
          const part = p.part
          if (part?.type !== 'tool' || !part.sessionID || parentOf.has(part.sessionID)) return
          const tool = toolName(String(part.tool ?? ''))
          const detail = detailOfInput(part.state?.input)
          const st = part.state?.status
          if (st === 'running') mutate(part.sessionID, s => (s.inflight.some(c => c.id === part.callID) ? s : onToolStart(s, part.callID, tool, detail)))
          else if (st === 'completed' || st === 'error')
            mutate(part.sessionID, s =>
              onToolEnd(s, { id: part.callID, tool, detail, isMain: true, didRun: true, hasFailed: st === 'error' }),
            )
          return
        }
        case 'permission.asked':
        case 'permission.updated': {
          const id = p.sessionID
          if (!id) return
          const root = parentOf.get(id) ?? id
          const askId = String(p.tool?.callID ?? p.id)
          const tool = toolName(String(p.permission ?? p.type ?? 'permission'))
          const detail = detailOfInput(p.metadata) || String((p.patterns ?? [])[0] ?? p.title ?? '')
          mutate(root, (s, x) => {
            x.asks.set(String(p.id), askId)
            return { ...onCheck(s, { id: askId, tool, detail, decision: 'ask', at: now }), promptedAt: now }
          })
          return
        }
        case 'permission.replied': {
          const id = p.sessionID
          if (!id) return
          const root = parentOf.get(id) ?? id
          mutate(root, (s, x) => {
            const askId = x.asks.get(String(p.requestID ?? p.permissionID)) ?? ''
            x.asks.delete(String(p.requestID ?? p.permissionID))
            const asks = s.asks.filter(a => a.id !== askId)
            const isDenied = p.reply === 'reject' || p.response === 'reject'
            return {
              ...s,
              asks,
              promptedAt: asks.length ? s.promptedAt : null,
              gate: isDenied ? { ...s.gate, denied: s.gate.denied + 1 } : s.gate,
            }
          })
          return
        }
      }
    },
    dispose: async () => {
      clearInterval(heartbeat)
      if (url) await Promise.all([...sessions.keys()].map(id => post('/api/end', { v: 1, sid: wireSid(id) }).catch(() => undefined)))
    },
  }
}
