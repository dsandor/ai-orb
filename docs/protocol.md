# Claude Orb wire protocol (v1)

The Claude Code mod (`mod/`) **pushes** to the device; the device never calls the host.
Transport: plain HTTP/1.1 on the LAN, JSON bodies, UTF-8. Device hostname: `claude-orb.local` (mDNS), port 80.
POSTs must carry `Content-Type: application/json` (otherwise 415), and a request with a browser `Origin` header
naming another host is refused, so a web page cannot forge pushes or settings changes. Requests whose `Host` is not
the device's own name (`<name>.local`) or IP get 403, which blocks DNS-rebinding attacks.
Optional shared secret: if the device has a token configured, every request must carry `X-Orb-Token: <token>`; otherwise 401.

All times are **relative durations in ms measured on the host at send time** (no clock sync needed). The device
records `millis()` on receipt and advances running timers itself.

## POST /api/state
Sent on change (debounced, at most every 400 ms) and as a heartbeat every 10 s. Full snapshot, never a diff.

```json
{
  "v": 1,
  "sid": "8f2c1a",                 // session id (short, stable for the session)
  "project": "aistatus",           // basename of the session cwd, <= 24 chars
  "state": "thinking",             // idle | thinking | tool | waiting | error | compacting
  "tool": "Bash",                  // current/last tool name, "" if none
  "detail": "npm test",            // short masked detail for the tool, <= 40 chars
  "model": "Opus 5.5",             // pretty model name
  "effort": "high",                // "" if unknown
  "mode": "default",               // permission mode: default | acceptEdits | plan | auto | bypassPermissions
  "ctx": { "pct": 42.5, "tokens": 85000, "window": 200000, "compactions": 1 },   // pct null if unknown
  "cost": 1.234,                   // session USD, null if unknown
  "limits": [ { "kind": "5h", "pct": 34, "resetInMs": 5400000 },              // 0..2 entries, resetInMs null if unknown
              { "kind": "7d", "pct": 12, "resetInMs": 302400000 } ],
  "turn": { "running": true, "elapsedMs": 41000, "steps": 7, "edits": 2, "errors": 0 },
  "last": { "durationMs": 63000, "costDelta": 0.21, "edits": 4, "errors": 0 },   // last completed turn, or null
  "agents": { "running": 2, "total": 3,
              "list": [ { "name": "Explore", "task": "find hooks", "status": "running",   // running | done | error
                          "ctx": 12000, "steps": 4, "elapsedMs": 30000 } ] },          // <= 4 entries, running first
  "gate": { "allowed": 31, "asked": 3, "denied": 1, "pending": 0 },
  "ttftMs": 820,                   // last main-loop time to first token, null if unknown
  "log": [ "edit core.ts", "bash: npm test", "spawn Explore" ]      // newest last, <= 4 entries, <= 32 chars each
}
```

`state` semantics (priority order when several apply): `waiting` = a permission check is pending (the user is needed);
`error` = last tool call failed and the turn is still running; `compacting`; `tool` = a tool call is in flight;
`thinking` = turn running, no tool in flight; `idle` = no turn running.

## POST /api/end
`{ "v": 1, "sid": "8f2c1a" }`: the session ended (exit or /clear). Device drops it.

## GET /api/info
Device answers `{ "name": "claude-orb", "fw": "0.1.0", "ip": "...", "sessions": 1, "uptimeMs": 123 }`.

## Device behaviour
- Holds up to 4 sessions keyed by `sid`. A session not heard from for 45 s is shown as stale (dimmed); after 5 min it is dropped.
- Focus: any session in `waiting` wins (attention screen). Otherwise the most recently changed non-idle session;
  with several busy sessions it rotates every 10 s.
- With no sessions: idle clock screen (NTP time) with "no sessions" and the device IP.
