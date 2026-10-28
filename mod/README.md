# deskorb: Claude Code mod for the Desk Orb display

Pushes the live state of each Claude Code session to a Desk Orb display: an ESP32-S3 with a 1.28" round GC9A01 screen (firmware in `../firmware`). The mod is a Claude Code mod (function hooks, Claude Code 2.1.287+). It watches the same session events Flightdeck reads. It doesn't need Flightdeck, and the two run side by side.

What it sends (see `../docs/protocol.md`): session state (thinking / tool / **waiting for you** / error / compacting / idle), current tool and target, model and effort, context fill and compactions, session cost, the 5h/7d rate limits with reset times, the running turn's clock, steps, edits and errors, the last turn's receipt, subagents (up to 4), permission totals, time to first token, and a 4-line log.

## How it reaches the display
- A snapshot goes out (POST `/api/state`) at most every 400 ms, and only when something changed.
- A heartbeat goes out every 10 s, so the device can tell a session that's gone quiet from one that's gone.
- When a session ends it sends POST `/api/end`.
- Requests go through Claude Code's own HTTP client. Nothing is written to disk. Commands and paths are credential-masked before they're sent.
- If the display can't be reached, the mod backs off (up to 60 s between tries) and shows `orb offline` in the status line after 3 failures in a row.

## Install
Inside Claude Code:
```
/plugin marketplace add dsandor/ai-orb
/plugin install deskorb@desk-orb
/reload-plugins
```
or from a clone, for one session: `claude --plugin-dir ./ai-orb/mod`.

## Configure (`/config`, or `pluginConfigs["deskorb@desk-orb"].options` in `~/.claude/settings.json`)
| Option | Default | |
|---|---|---|
| `deviceUrl` | `http://claude-orb.local` | the display, or its IP if mDNS is unreliable on your network |
| `token` | empty | must match the token on the device's settings page |
| `enabled` | `true` | |

## Commands
- `/orb` shows the device URL and the result of the last push.
- `/orb test` sends a snapshot now and reports the result.

## Develop
```sh
claude plugin validate mod
claude plugin test mod
```
