# AI Orb firmware

ESP32-S3 + round 1.28" GC9A01 (240x240) display that shows live Claude Code
session status pushed to it over Wi-Fi. The Claude Code side (`../mod/`) POSTs
JSON snapshots to the device. The wire contract is
[`../docs/protocol.md`](../docs/protocol.md).

- LovyanGFX, full-frame 16-bit canvas (PSRAM if present), anti-aliased arcs and
  Inter smooth fonts, about 30 fps while busy and 15 fps when idle
- Wi-Fi setup through a captive portal, settings in NVS, mDNS `claude-orb.local`,
  NTP clock
- HTTP API on port 80 (`/api/state`, `/api/end`, `/api/info`) with an optional
  `X-Orb-Token`

## Hardware

### `esp32s3_round`: ESP32-S3 Super Mini + bare GC9A01 module (default)

Same wiring as the BambuHelper `esp32s3_round` env.

| GC9A01 pin | ESP32-S3 Super Mini |
|------------|---------------------|
| VCC        | 3V3                 |
| GND        | GND                 |
| SCL / SCK  | GPIO 12             |
| SDA / MOSI | GPIO 11             |
| DC         | GPIO 9              |
| CS         | GPIO 10             |
| RST / RES  | GPIO 8              |
| BLK        | not connected (7-pin modules have the backlight on all the time) |

### `waveshare_s3_lcd128`: Waveshare ESP32-S3-LCD-1.28 (integrated) **UNTESTED**

The panel is on board: SCLK 10, MOSI 11, CS 9, DC 8, RST 14, backlight PWM on
GPIO 2. These pins come from the vendor pinout and have not been verified on
hardware.

### Optional button

Wire a push button between a free GPIO and GND, then set `-D PIN_BUTTON=<gpio>`
in the env (the internal pull-up is used).

- **Short press**: next page, and after the last page the next session. This
  holds the view for 30 s.
- **Long press** (0.7 s): device info overlay (host, IP, RSSI, fw, heap). Press
  again to close it.

### Changing pins / panel options

Everything lives in `build_flags` in `platformio.ini`:

| Flag | Meaning |
|------|---------|
| `PIN_SCLK PIN_MOSI PIN_DC PIN_CS PIN_RST` | SPI wiring |
| `PIN_BL` | backlight GPIO (PWM), `-1` = always on |
| `PIN_BUTTON` | button GPIO, `-1` = none |
| `PANEL_INVERT` | `1` for most GC9A01 modules. Flip it if colours look like a photo negative |
| `PANEL_RGB_ORDER` | `0` = RGB, `1` = BGR. Flip it if red and blue are swapped (Claude orange shows as blue) |
| `PANEL_SPI_FREQ` | default 40 MHz. Many GC9A01s run at 80 MHz, which roughly halves push time |
| `ORB_CANVAS_SRAM_FIRST` | put the frame buffer in internal SRAM instead of PSRAM |

You can change the screen rotation (0/90/180/270) on the settings page without
rebuilding.

## Build and flash

```sh
cd firmware
pio run -e esp32s3_round                 # build
pio run -e esp32s3_round -t upload       # flash over USB-C
pio device monitor -e esp32s3_round      # serial log, 115200
```

Use `-e waveshare_s3_lcd128` for the Waveshare board. If the S3 does not show
up as a serial port, hold BOOT while plugging it in.

> **Build note:** if `pio` on your PATH runs under a global
> Python (e.g. pyenv), which is missing the `intelhex` module that esptool in
> `espressif32@6.12.0` needs ("ModuleNotFoundError: No module named
> 'intelhex'"). Either use PlatformIO's own interpreter,
> `~/.platformio/penv/bin/pio run -e esp32s3_round`, or run
> `pip install intelhex` for that Python.

## First boot / Wi-Fi setup

1. Power the orb. With no saved Wi-Fi it starts an open access point,
   **`AI-Orb-Setup`**, and the screen shows instructions.
2. Join that network from a phone or laptop. The captive portal should open on
   its own; if it doesn't, browse to `http://192.168.4.1`.
3. Pick your network, enter the password, and optionally set a name (the mDNS
   host, default `claude-orb`), an API token, and a time zone (your browser's
   zone is preselected). Then press **Save & connect**.
4. The orb reboots and joins your network. A "connected" splash shows its IP
   and `claude-orb.local`.

If the saved network can't be joined within 20 s at boot, the orb falls back to
the setup AP. It keeps retrying the saved network every 60 s while nobody is
connected to the portal.

## Settings page

Open `http://claude-orb.local/` (or the IP). It shows:

- Status: Wi-Fi/RSSI, uptime, API request count, auth mode, canvas and heap,
  plus a live session list.
- Settings: name, API token (set, change or remove), time zone (common zones or
  a custom POSIX TZ string), page rotation interval (3 to 60 s, default 8),
  screen rotation, 24-hour clock, and brightness (only on boards with a
  backlight pin).
- Maintenance: **Forget Wi-Fi** (reboots into setup mode) and **Factory
  reset** (erases all settings).

When a token is configured, saving changes requires typing the current token. The token is never shown on any page. The `/setup` form works only while the orb is in setup mode. Forms posted from another site are refused.

## API (summary, see `docs/protocol.md`)

| Method | Path | Notes |
|--------|------|-------|
| POST | `/api/state` | full session snapshot. Returns `{"ok":true,"sessions":N}`, `400` on bad JSON or missing `sid`, `413` above 8 KB, `415` without `Content-Type: application/json` |
| POST | `/api/end`   | `{"v":1,"sid":"..."}`. Unknown sids are fine (200) |
| GET  | `/api/info`  | `{"name","fw","ip","sessions","uptimeMs","board","proto"}` |

If a token is set, every `/api/*` request must send `X-Orb-Token: <token>`;
without it the device returns `401`.

```sh
curl -s http://claude-orb.local/api/info
curl -s -XPOST http://claude-orb.local/api/state -H 'Content-Type: application/json' \
     -d '{"v":1,"sid":"t1","project":"demo","state":"thinking","turn":{"running":true,"elapsedMs":0}}'
```

## Testing without Claude Code: `tools/simulate.py`

Python 3, stdlib only. It speaks the protocol: snapshots on change (at least
400 ms apart), a heartbeat every 10 s, and `/api/end` on Ctrl-C.

```sh
python3 tools/simulate.py --scenario cycle                    # walk through everything
python3 tools/simulate.py --scenario thinking
python3 tools/simulate.py --scenario tools                    # tool calls, errors, compaction
python3 tools/simulate.py --scenario agents                   # 2-4 subagents
python3 tools/simulate.py --scenario waiting                  # permission prompt -> NEEDS YOU
python3 tools/simulate.py --scenario multi --sessions 3       # concurrent sessions + rotation
python3 tools/simulate.py --host 192.168.1.42 --token s3cret --scenario multi
python3 tools/simulate.py --info                              # GET /api/info
python3 tools/simulate.py --scenario tools --dry-run          # print JSON lines, send nothing
```

To test staleness, stop the simulator with `kill -9` (no `/api/end`). After
45 s the session dims with "no signal", and after 5 minutes it is dropped.

## Demo mode (for videos)

`esp32s3_round_demo` is the normal firmware plus a scripted 112-second loop that
plays made-up sessions through every screen, with no Claude Code or simulator
needed:

```sh
pio run -e esp32s3_round_demo -t upload
```

| Seconds | Screen |
|---|---|
| 0-4 | idle clock |
| 4-27 | one session thinking, then Read / Grep / Edit / Bash tool calls |
| 27-37 | Agents page: three subagents finishing one by one |
| 37-45 | Limits page |
| 45-53 | **NEEDS YOU** (`git push origin main`) |
| 53-62 | the approved command runs, then **DONE** |
| 62-92 | three sessions: `website` thinking, `infra` compacting then failing, `website` **NEEDS YOU** |
| 94-104 | **DONE** for each of the three sessions |
| 105-112 | sessions end, back to the clock |

Wi-Fi still runs so the clock is right. Pushes from the mod are answered but
ignored, so a Claude Code session nearby can't interrupt the loop. To go back,
flash `esp32s3_round` again.

## Screens

All screens are laid out for the circle; nothing important goes in the corners.
Rings sweep from 7:30 to 4:30, which leaves the bottom gap for the tool line
and the session dots.

| Screen | When | What it shows |
|--------|------|---------------|
| **Session** | focused session (main page) | Outer ring: context gauge, green to amber to red, with a moving shimmer while busy and purple pips for compactions. Inner ring: 5h rate limit. Centre: ctx % and tokens, project, model, effort and permission-mode tag, the animated spark, the turn timer (counted locally from `elapsedMs`), cost, steps, errors and edits. Bottom: current tool and a scrolling detail line |
| **Agents** | page 2, only if the session has agents | Segmented outer ring (one segment per agent). Up to 4 lanes, each with a status spinner, name, elapsed time, task, step count and an activity rail |
| **Limits** | last page | 5h (outer) and 7d (inner) limit arcs with reset countdowns, the last turn receipt (duration, cost delta, edits, errors), gate totals and ttft |
| **Attention** | any fresh session in `waiting` | Pulsing amber rim with comets, "NEEDS YOU", project, tool, detail, and a waiting timer. Takes over everything else |
| **Done** | a turn just finished (busy to idle) | 3 s green flash: rim pulses, ripples, the check mark draws itself, then duration, cost and edits |
| **Idle clock** | no sessions, or all idle for 2 min | NTP time, a smooth seconds comet, date, session summary, `claude-orb.local` and the IP |
| **Setup / connecting / connected** | boot | AP instructions, a spinner with the SSID, or the IP splash |
| **Info** | long press | host, IP, RSSI, fw, sessions, requests, uptime, heap, canvas |

The spark's motion encodes the state: **thinking** is a slow spin with a
breathing wave, **tool** is a ray-to-ray chase plus an orbiting satellite,
**compacting** squeezes in purple, **waiting** is an amber heartbeat, **error**
is red and shakes, and **idle** is still.

Focus rules follow the protocol. A waiting session wins (several waiting ones
rotate every 10 s). Otherwise the most recently changed busy session is
focused, and several busy sessions rotate every 10 s; while rotating, each one
shows only its main Session page. A single focused session cycles through its
pages every *rotation interval*. Dots at the bottom show each session: orange
for the focused one, amber for waiting, faint for stale. A session not heard
from for 45 s is dimmed and labelled "no signal".

## Desktop preview (no hardware needed)

`preview/build.zsh` compiles the real `gfx`, `ui_*` and `sessions` code against
LovyanGFX's desktop backend. It feeds sample sessions through the real parser
and writes a contact sheet of every screen to `preview/out/sheet.png`. It needs
SDL2 headers (`brew install sdl2`) and one prior `pio run`, so the libraries
are already downloaded.

## Layout

```
platformio.ini        envs + pin flags
include/config.h      constants (timeouts, limits, defaults for pins)
include/fonts/        Inter VLW fonts (12/16/22 px, 32/60 px digits) from BambuHelper
src/main.cpp          setup/loop, optional button
src/net.cpp           Wi-Fi STA + AP portal fallback, mDNS, NTP
src/web.cpp           HTTP API, settings page, setup portal
src/sessions.cpp      defensive JSON parsing, session table, staleness, focus
src/settings.cpp      NVS (Preferences) persistence
src/gfx.cpp           GC9A01 panel, canvas, AA arc/disc/glow/wedge, fonts, text
src/ui.cpp            view selection, page rotation, fades, frame pacing
src/ui_common.cpp     spark glyph, gauges, dots, formatting
src/ui_session.cpp    Session screen
src/ui_agents.cpp     Agents screen
src/ui_limits.cpp     Limits / receipt screen
src/ui_attention.cpp  Attention + Done screens
src/ui_idle.cpp       Idle clock, setup/connecting/connected, info overlay
src/demo.cpp          scripted mock sessions for the demo build
preview/              desktop renderer for screenshots
```
