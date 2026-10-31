# deskorb: Claude Code mod for the AI Orb display

Pushes the live state of each Claude Code session to a AI Orb display: an ESP32-S3 with a 1.28" round GC9A01 screen (firmware in `../firmware`). The mod is a Claude Code mod (function hooks, Claude Code 2.1.287+). It watches the same session events Flightdeck reads. It doesn't need Flightdeck, and the two run side by side.

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
| `deviceUrl` | `http://claude-orb.local` | the display, or its IP if mDNS is unreliable on your network; a non-default value turns off the automatic scan |
| `token` | empty | must match the token on the device's settings page |
| `enabled` | `true` | |

## Commands
- `/orb` shows the device URL, where it came from, and the result of the last push.
- `/orb test` sends a snapshot now and reports the result.
- `/orb scan`, `/orb choose`, `/orb use <name, IP or URL>`, `/orb reset`: see below.

### More than one orb
At the start of a session the mod looks for orbs on your network (about 2 s, in the background), as long as `deviceUrl` is still the default and nothing has been chosen yet:
- **One orb found:** it is used, whatever its name.
- **Several found:** Claude Code asks which one this computer's sessions should show on. The answer is saved for every session on this computer, so you're asked once.

You can change it any time:
| Command | |
|---|---|
| `/orb scan` | lists the orbs on your network; `*` marks the one this computer reports to |
| `/orb choose` | scans and asks you to pick one |
| `/orb use <name, IP or URL>` | picks one by hand, e.g. `/orb use desk.local` or `/orb use 192.168.1.50` |
| `/orb reset` | forgets the pick and goes back to `deviceUrl` |

Give each orb its own name on its settings page (e.g. `desk-orb`, `kitchen-orb`). Two orbs that are both named `claude-orb` still work, but one of them can only be reached by its IP, which can change.
Setting `deviceUrl` in `/config` to anything other than the default turns the automatic scan off, and a pick made under one `deviceUrl` is dropped when you change it. Scanning needs `dns-sd` (built into macOS) or `avahi-browse` (Linux, package `avahi-utils`). On Windows, set `deviceUrl` or use `/orb use <IP>`.

## Develop
```sh
claude plugin validate mod
claude plugin test mod
```
