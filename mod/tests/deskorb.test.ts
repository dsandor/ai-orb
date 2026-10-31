import { expect, mock, test } from 'claude-code/testing'

import {
  ASK_GRACE_MS,
  DEFAULT_STATE,
  baseUrl,
  deviceLabel,
  deviceUrl,
  detailOf,
  fit,
  normalize,
  onAgentEnd,
  onAgentStep,
  onCheck,
  onSpawn,
  onToolEnd,
  onToolStart,
  onTurnEnd,
  onTurnStart,
  parseDiscovery,
  pickDevice,
  prettyModel,
  signature,
  stateOf,
  toWire,
} from '../hooks/core'

// ---------------------------------------------------------------- pure behaviour

test('a stored value of another shape reads as the defaults', () => {
  expect(normalize(undefined)).toEqual(DEFAULT_STATE)
  expect(normalize({ schema: 0, model: 'x' })).toEqual(DEFAULT_STATE)
  expect(normalize({ ...DEFAULT_STATE, agents: 'nope' }).agents).toEqual([])
})

test('the state word follows the protocol priority', () => {
  let s = onTurnStart(DEFAULT_STATE, 1000, 0.5)
  expect(stateOf(s, 1000)).toBe('thinking')
  s = onToolStart(s, 't1', 'Bash', 'npm test')
  expect(stateOf(s, 1000)).toBe('tool')
  s = onCheck(s, { id: 't1', tool: 'Bash', detail: 'npm test', decision: 'ask', at: 1000 })
  // An ask the mode may still settle by itself is not yet the person's.
  expect(stateOf(s, 1000 + ASK_GRACE_MS - 1)).toBe('tool')
  expect(stateOf(s, 1000 + ASK_GRACE_MS)).toBe('waiting')
  expect(stateOf({ ...s, promptedAt: 1001 }, 1001)).toBe('waiting')
  s = onToolEnd(s, { id: 't1', tool: 'Bash', detail: 'npm test', isMain: true, didRun: true, hasFailed: true })
  expect(stateOf(s, 9000)).toBe('error')
  expect(s.turnErrors).toBe(1)
  s = onTurnEnd(s, 5000, 0.75)
  expect(stateOf(s, 9000)).toBe('idle')
  expect(s.last).toEqual({ durationMs: 5000, costDelta: 0.25, edits: 0, errors: 1 })
})

test('an ask refused by the person counts as denied', () => {
  let s = onCheck(DEFAULT_STATE, { id: 'a', tool: 'Write', detail: 'x.ts', decision: 'ask', at: 0 })
  s = onToolEnd(s, { id: 'a', tool: 'Write', detail: 'x.ts', isMain: true, didRun: false, hasFailed: false })
  expect(s.gate).toEqual({ allowed: 0, asked: 1, denied: 1 })
  expect(s.asks).toEqual([])
})

test('agents: spawn, steps, end; running ones listed first', () => {
  let s = onSpawn(DEFAULT_STATE, { id: 'a1', name: 'Explore', task: 'find hooks', now: 0 })
  s = onSpawn(s, { id: 'a2', name: 'Plan', task: 'plan it', now: 10 })
  s = onAgentStep(s, 'a1', 12000)
  s = onAgentEnd(s, 'a1', 'answer', 500)
  const w = toWire(s, 'sid', 1000)
  expect(w.agents.running).toBe(1)
  expect(w.agents.total).toBe(2)
  expect(w.agents.list.map(a => a.name)).toEqual(['Plan', 'Explore'])
  expect(w.agents.list[1]).toMatchObject({ status: 'done', ctx: 12000, steps: 1, elapsedMs: 500 })
})

test('the wire is ASCII, short, and masks credentials', () => {
  expect(detailOf({ command: 'curl -H "Authorization: Bearer abc.def.ghi123" x' })).not.toContain('abc.def')
  expect(fit('héllo → wörld', 40)).toBe('hllo > wrld')
  expect(fit('x'.repeat(50), 10)).toBe('xxxxxxx...')
  const s = onToolStart({ ...DEFAULT_STATE, project: 'p'.repeat(40) }, 't', 'Bash', 'y'.repeat(100))
  const w = toWire(s, 'sid', 0)
  expect(w.project.length).toBeLessThanOrEqual(24)
  expect(w.detail.length).toBeLessThanOrEqual(40)
})

test('the signature ignores clocks, so a heartbeat is the only repeat', () => {
  const s = onTurnStart(DEFAULT_STATE, 0, null)
  expect(signature(toWire(s, 'x', 1000))).toBe(signature(toWire(s, 'x', 5000)))
  expect(signature(toWire(s, 'x', 1000))).not.toBe(signature(toWire(onToolStart(s, 't', 'Read', 'a.ts'), 'x', 1000)))
})

test('models and URLs read as people write them', () => {
  expect(prettyModel('claude-opus-5-5')).toBe('Opus 5.5')
  expect(prettyModel('claude-sonnet-4-5-20250929[1m]')).toBe('Sonnet 4.5 1M')
  expect(baseUrl('claude-orb.local/')).toBe('http://claude-orb.local')
  expect(baseUrl(' https://10.0.0.5 ')).toBe('https://10.0.0.5')
})

// ---------------------------------------------------------------- finding orbs

const DNS_SD = `#dns-sd
Browsing for _claude-orb._tcp.local
_claude-orb._tcp                                PTR     desk._claude-orb._tcp
desk._claude-orb._tcp                           SRV     0 0 80 desk.local. ; Replace with unicast FQDN of target host
desk._claude-orb._tcp                           TXT     "fw=0.1.0" "proto=1" "api=/api/state" "name=desk" "ip=192.168.1.50"
claude-orb._claude-orb._tcp                     SRV     0 0 80 claude-orb.local. ; Replace with unicast FQDN of target host
claude-orb._claude-orb._tcp                     TXT     "fw=0.1.0" "proto=1" "api=/api/state" "name=claude-orb" "ip=192.168.1.60"
claude-orb\\0322._claude-orb._tcp              SRV     0 0 80 claude-orb-2.local. ; Replace with unicast FQDN of target host
claude-orb\\0322._claude-orb._tcp              TXT     "fw=0.1.0" "proto=1" "api=/api/state" "name=claude-orb" "ip=192.168.1.61"
Browsing for _http._tcp.local
desk._http._tcp                                 SRV     0 0 80 desk.local. ; Replace with unicast FQDN of target host
desk._http._tcp                                 TXT     "fw=0.1.0" "proto=1" "api=/api/state" "name=desk" "ip=192.168.1.50"
EPSON\\032ET-3830._http._tcp                    SRV     0 0 80 EPSON457210.local. ; Replace with unicast FQDN of target host
EPSON\\032ET-3830._http._tcp                    TXT     ""
old._http._tcp                                  SRV     0 0 80 old-orb.local. ; Replace with unicast FQDN of target host
old._http._tcp                                  TXT     "fw=0.0.9" "proto=1" "api=/api/state"
`

test('dns-sd: orbs found once each, printers left out, a renamed twin reached by IP', () => {
  const ds = parseDiscovery(DNS_SD)
  expect(ds.map(deviceLabel)).toEqual(['claude-orb (192.168.1.60)', 'claude-orb (192.168.1.61)', 'desk (192.168.1.50)', 'old-orb (old-orb.local)'])
  expect(ds.map(deviceUrl)).toEqual(['http://claude-orb.local', 'http://192.168.1.61', 'http://desk.local', 'http://old-orb.local'])
})

test('avahi: resolved IPv4 rows', () => {
  const out = [
    '#avahi',
    '+;eth0;IPv4;desk;_claude-orb._tcp;local',
    '=;eth0;IPv6;desk;_claude-orb._tcp;local;desk.local;fe80::1;80;"name=desk"',
    '=;eth0;IPv4;desk;_claude-orb._tcp;local;desk.local;192.168.1.50;80;"fw=0.1.0" "name=desk" "ip=192.168.1.50"',
    '=;eth0;IPv4;Kitchen\\032TV;_http._tcp;local;tv.local;192.168.1.9;8080;',
  ].join('\n')
  expect(parseDiscovery(out)).toEqual([{ name: 'desk', host: 'desk.local', ip: '192.168.1.50', port: 80, fw: '0.1.0' }])
  expect(parseDiscovery('#none\n')).toEqual([])
})

test('an answer names an orb by label, name, IP, or a typed URL', () => {
  const ds = parseDiscovery(DNS_SD)
  expect(pickDevice('desk (192.168.1.50)', ds)).toEqual({ url: 'http://desk.local', label: 'desk (192.168.1.50)' })
  expect(pickDevice('192.168.1.61', ds)?.url).toBe('http://192.168.1.61')
  expect(pickDevice('DESK', ds)?.url).toBe('http://desk.local')
  expect(pickDevice('10.0.0.7', ds)).toEqual({ url: 'http://10.0.0.7', label: '10.0.0.7' })
  expect(pickDevice('no idea which', ds)).toBeNull()
})

// ---------------------------------------------------------------- through the engine

test('a session start pushes a snapshot to the device', { options: { deviceUrl: 'http://orb.test', token: 'sekrit' } }, async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const sent: { url: string; headers: Record<string, string>; body: string }[] = []
  on('command.register', () => ({ value: { command: 'orb' } }))
  on('session.id', () => ({ value: 'session-0123456789abcdef' }))
  on('session.usage', () => ({
    value: {
      startedAt: 0,
      context: { window: 200_000, tokens: 50_000, percent: 25 },
      rateLimits: [{ kind: 'five_hour', percentUsed: 40, resetsAt: new Date(1_000_000 + 3_600_000).toISOString() }],
      cost: { usd: 1.5 },
    },
  }))
  on('http.fetch', (_, e) => {
    sent.push({ url: e.url, headers: e.init?.headers ?? {}, body: e.init?.body ?? '' })
    return { value: { status: 200, ok: true, headers: {}, text: '' } }
  })
  on('session.start', (_, e) => ({ cwd: e.cwd }))

  await $.session.start({ cwd: '/Users/me/Projects/aistatus', surface: 'terminal', isInteractive: true })
  await clock.advance(100)
  await clock.settle()

  expect(sent.length).toBe(1)
  const first = sent[0]!
  expect(first.url).toBe('http://orb.test/api/state')
  expect(first.headers['X-Orb-Token']).toBe('sekrit')
  const w = JSON.parse(first.body)
  expect(w).toMatchObject({ v: 1, sid: 'session-0123', project: 'aistatus', state: 'idle', cost: 1.5 })
  expect(w.ctx).toMatchObject({ pct: 25, tokens: 50_000, window: 200_000 })
  // Pushed 50 ms after the start, so 50 ms less remains.
  expect(w.limits).toEqual([{ kind: '5h', pct: 40, resetInMs: 3_600_000 - 50 }])

  // Nothing changed: no push until the heartbeat.
  await clock.advance(5_000)
  await clock.settle()
  expect(sent.length).toBe(1)
  await clock.advance(6_000)
  await clock.settle()
  expect(sent.length).toBe(2)
})

test('several orbs found: the person picks one, it is kept, and pushes go there', { options: { deviceUrl: 'http://claude-orb.local' } }, async ($, on) => {
  const clock = mock.clock(on, { now: 1_000_000 })
  const stored: Record<string, unknown> = {}
  on('store.get', (_, e) => ({ value: stored[e.key] }))
  on('store.set', (_, e) => {
    stored[e.key] = e.value
    return { value: undefined }
  })
  const logged: string[] = []
  on('ui.log', (_, e) => {
    logged.push(e.text)
    return { value: undefined }
  })
  const urls: string[] = []
  const asked: string[][] = []
  on('command.register', () => ({ value: { command: 'orb' } }))
  on('session.id', () => ({ value: 'session-0123456789abcdef' }))
  on('session.usage', () => ({ value: { startedAt: 0, context: { window: 200_000 }, rateLimits: [] } }))
  on('process.run', () => ({ value: { exitCode: 0, stdout: DNS_SD, stderr: '', isStdoutTruncated: false, isStderrTruncated: false } }))
  on('tool.call', { tool: 'AskUserQuestion' }, (_, e) => {
    const q = e.questions[0]!
    asked.push(q.options.map(o => o.label))
    return { result: { questions: e.questions, answers: { [q.question]: 'desk (192.168.1.50)' } } }
  })
  on('http.fetch', (_, e) => {
    urls.push(e.url)
    return { value: { status: 200, ok: true, headers: {}, text: '' } }
  })
  on('session.start', (_, e) => ({ cwd: e.cwd }))

  await $.session.start({ cwd: '/Users/me/Projects/aistatus', surface: 'terminal', isInteractive: true })
  await clock.advance(100)
  await clock.settle()

  expect(asked).toEqual([['claude-orb (192.168.1.60) - current', 'claude-orb (192.168.1.61)', 'desk (192.168.1.50)', 'old-orb (old-orb.local)']])
  expect(logged[0]).toContain('desk (192.168.1.50)')
  expect(stored.device).toEqual({ url: 'http://desk.local', label: 'desk (192.168.1.50)', configUrl: 'http://claude-orb.local' })
  expect(urls[urls.length - 1]).toBe('http://desk.local/api/state')
})
