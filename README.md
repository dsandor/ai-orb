# AI Orb

A round desk display for Claude Code, inspired by [BambuHelper](https://github.com/Keralots/BambuHelper). An ESP32-S3 with a 1.28" GC9A01 screen shows what each of your Claude Code sessions is doing: thinking or running a tool, context fill, cost, rate limits, subagents and permission totals. When a session is waiting for you to approve something, the whole display pulses amber.

```
Claude Code session ──(deskorb mod)── HTTP POST on your LAN ──▶ ESP32-S3 + GC9A01  (claude-orb.local)
```

The display only receives. A small Claude Code mod pushes a JSON snapshot whenever something changes, plus a heartbeat every 10 s. No server or daemon is needed, and nothing leaves your network.

| Folder | What |
|---|---|
| [`mod/`](mod/README.md) | the Claude Code mod (`deskorb`) that pushes status |
| [`firmware/`](firmware/README.md) | PlatformIO firmware for the ESP32-S3 + GC9A01 |
| [`docs/protocol.md`](docs/protocol.md) | the JSON contract between the two |
| `tools/simulate.py` | drives the display with fake sessions, no Claude Code needed |

## What you need
- An ESP32-S3 Super Mini and a 1.28" GC9A01 round SPI module (the 7-pin kind), or a Waveshare ESP32-S3-LCD-1.28 (untested).
- [PlatformIO](https://platformio.org/install/cli) to build and flash.
- Claude Code **2.1.287 or newer** (mods support).
- The computer running Claude Code and the display on the same network.

## 1. Wire it (ESP32-S3 Super Mini)
| GC9A01 | ESP32-S3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCL | GPIO 12 |
| SDA | GPIO 11 |
| DC | GPIO 9 |
| CS | GPIO 10 |
| RST | GPIO 8 |

This is the same wiring as BambuHelper's `esp32s3_round` build. Other pins are one line each in `firmware/platformio.ini`.

## 2. Flash the firmware
```sh
git clone https://github.com/dsandor/ai-orb.git
cd ai-orb/firmware
pio run -e esp32s3_round -t upload
```
Use `-e waveshare_s3_lcd128` for the Waveshare board. If the board doesn't show up as a serial port, hold BOOT while plugging it in.

## 3. Connect it to Wi-Fi
1. On first boot the display shows **Wi-Fi setup** and opens an access point called **`Claude-Orb-Setup`**.
2. Join it from your phone or laptop. The setup page opens by itself; if it doesn't, go to `http://192.168.4.1`.
3. Pick your network and enter the password. You can also set a device name, an API token and a time zone here.
4. The display reboots, joins your network, and shows its IP and `claude-orb.local`.

To change settings later, open `http://claude-orb.local/`.

## 4. Check the display (optional)
```sh
python3 tools/simulate.py --host claude-orb.local --scenario cycle
```
This walks through every screen with fake sessions; press Ctrl-C to stop. Other scenarios are `thinking`, `tools`, `agents`, `waiting` and `multi`, and `--sessions N` fakes several sessions at once. Add `--token <token>` if you set a token.

For a video, flash the demo build instead. It loops through every screen by itself, with no computer attached: `pio run -e esp32s3_round_demo -t upload` (details in [`firmware/README.md`](firmware/README.md#demo-mode-for-videos)). Flash `esp32s3_round` again to go back.

## 5. Install the mod in Claude Code
On every computer whose sessions you want on the display, run inside Claude Code:
```
/plugin marketplace add dsandor/ai-orb
/plugin install deskorb@desk-orb
/reload-plugins
/orb test
```
`/orb test` should answer `ok`. From then on every session on that computer shows up on the display by itself.

Settings are in `/config`, or under `pluginConfigs["deskorb@desk-orb"].options` in `~/.claude/settings.json`:

| Option | Default | |
|---|---|---|
| `deviceUrl` | `http://claude-orb.local` | use the display's IP if `.local` names don't resolve on your network |
| `token` | empty | must match the token set on the display |
| `enabled` | `true` | turns pushing off without uninstalling |

Commands:
- `/orb` shows the device URL and the result of the last push.
- `/orb test` sends a snapshot now.

To update: `claude plugin update deskorb@desk-orb`, then `/reload-plugins`.

## What the display shows
- **Session:**
  - Outer ring: context fill, going green to amber to red, with dots for compactions.
  - Inner ring: the 5-hour limit.
  - Centre: an animated spark whose motion changes with the state (thinking, tool, compacting, error), the turn timer, cost, steps and edits.
  - Bottom: the current tool and its target.
- **Agents:** up to 4 subagents, each with its status, task, steps and elapsed time.
- **Limits:** 5-hour and 7-day limits with reset countdowns, the last turn (duration, cost, edits, errors), permission totals, and time to first token.
- **Needs you:** a pulsing amber ring with the project, tool and command. It takes over when any session is waiting for a permission answer.
- **Done:** a green check flash when a turn finishes.
- **Idle:** a clock, shown when there are no sessions or nothing has happened for 2 minutes.

Pages rotate every 8 s, and several busy sessions take turns every 10 s. Up to 4 sessions are tracked. A session that stops reporting is dimmed after 45 s and dropped after 5 minutes. An optional push button (`PIN_BUTTON`) steps through pages; a long press shows device info.

## Troubleshooting
- **`orb offline` in the Claude Code status line:** the mod couldn't reach the display 3 times in a row. Run `/orb` to see the error. Check the display is on the same network, and try its IP as `deviceUrl`.
- **Colours look like a photo negative:** set `PANEL_INVERT=0` in `firmware/platformio.ini` and reflash.
- **Orange shows as blue:** set `PANEL_RGB_ORDER=1` and reflash.
- **Picture is rotated:** change the rotation on the settings page.
- **"Needs you" stays up while an approved command runs:** a known limit. Claude Code doesn't tell mods the moment you approve, so it clears when that command finishes.
- **`No module named 'intelhex'` when flashing:** use PlatformIO's own `~/.platformio/penv/bin/pio`, or run `pip install intelhex`.

## Develop
```sh
claude plugin validate mod && claude plugin test mod          # the mod
cd firmware && pio run -e esp32s3_round                       # the firmware
cd firmware/preview && ./build.zsh                            # render every screen to preview/out/sheet.png on a Mac
```

## License
MIT. Parts of the mod are adapted from [Flightdeck](https://github.com/scasella/claude-flightdeck) (MIT). The Inter fonts are under the SIL Open Font License.
