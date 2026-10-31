# deskorb for OpenCode

Shows your OpenCode sessions on the Desk Orb, the same way the Claude Code mod does. It uses the same state logic and wire format (`../mod/hooks/core.ts`, `../docs/protocol.md`). It's written for OpenCode's **V1 plugin API** (opencode 1.x, `opencode --version`). OpenCode 2.x plugins use a different API, and this one won't load there.

## Install
The plugin imports code from `../mod`, so install it as a **symlink** into the clone, not as a copy:

```sh
git clone https://github.com/dsandor/ai-orb.git ~/ai-orb      # or wherever you keep it
mkdir -p ~/.config/opencode/plugins
ln -s ~/ai-orb/opencode/deskorb.ts ~/.config/opencode/plugins/deskorb.ts
```
Restart OpenCode. Every OpenCode session on this computer now shows up on the orb. To load it for one project only, put the symlink in that project's `.opencode/plugins/` instead.

To update, run `git -C ~/ai-orb pull` and restart OpenCode.

## Pick the orb
The first of these that applies wins:

1. **Plugin options**, if you list the plugin in `opencode.json` instead of symlinking it. Use an absolute path, and don't also symlink it, or it loads twice:
   ```json
   { "plugin": [["/Users/you/ai-orb/opencode/deskorb.ts", { "deviceUrl": "http://desk-orb.local", "token": "" }]] }
   ```
2. **Environment variables:** `ORB_URL=http://192.168.1.50` and `ORB_TOKEN=...`.
3. **A saved choice**, made with the chooser:
   ```sh
   python3 ~/ai-orb/tools/orb-choose.py           # lists the orbs on your network and asks which one
   python3 ~/ai-orb/tools/orb-choose.py --url 192.168.1.50 --token T
   python3 ~/ai-orb/tools/orb-choose.py --reset
   ```
   This writes `~/.config/deskorb/device.json`. Running sessions pick up a change within 10 s.
4. **Automatic:** with none of the above, the plugin scans the network when OpenCode starts. If it finds one orb, it uses it. If it finds several, it shows a toast listing them and pushes nothing until you pick one with the chooser.

Scanning needs `dns-sd` (macOS) or `avahi-browse` (Linux, package `avahi-utils`). On Windows, use options or `ORB_URL`.

## What's different from Claude Code
OpenCode doesn't report a context window size or rate limits to plugins, so the context ring shows tokens without a percentage and the limits page stays empty. Subagents are shown by their task title. Everything else works as it does in Claude Code: the state, tool and target, permission asks ("needs you"), cost, turn receipts and the log.
