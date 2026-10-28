#!/usr/bin/env python3
"""Claude Orb simulator - pushes fake Claude Code sessions to the device.

Implements the host side of docs/protocol.md (v1) so the display can be tested
without Claude Code: full snapshots via POST /api/state on every change
(debounced to >= 400 ms) plus a heartbeat every 10 s, and POST /api/end on exit.

Examples:
  python3 tools/simulate.py --scenario cycle
  python3 tools/simulate.py --host 192.168.1.42 --scenario multi --sessions 3
  python3 tools/simulate.py --scenario waiting --token s3cret
  python3 tools/simulate.py --info
  python3 tools/simulate.py --scenario tools --dry-run      # print JSON, send nothing

Stdlib only (urllib). Ctrl-C ends the simulated sessions on the device.
"""

import argparse
import json
import random
import sys
import time
import urllib.error
import urllib.request

SCENARIOS = ("thinking", "tools", "agents", "waiting", "multi", "cycle")

PROJECTS = ["aistatus", "website", "infra", "api-gateway", "mobile-app", "data-pipeline"]
MODELS = [("Opus 5.5", "high"), ("Sonnet 5", "medium"), ("Opus 5.5", "max"), ("Haiku 4.5", "")]
TOOLS = [
    ("Bash", "npm test"),
    ("Bash", "git status --short"),
    ("Bash", "pytest -q tests/test_api.py"),
    ("Read", "src/core/session.ts"),
    ("Grep", "registerHook"),
    ("Glob", "src/**/*.tsx"),
    ("Edit", "src/core/session.ts"),
    ("Write", "docs/protocol.md"),
    ("WebFetch", "docs.anthropic.com"),
    ("Edit", "firmware/src/ui_session.cpp"),
]
AGENT_TASKS = [
    ("Explore", "find hook registration points"),
    ("code-reviewer", "review session.ts changes"),
    ("Plan", "design the wire protocol"),
    ("test-runner", "run the integration suite"),
]

MIN_SEND_GAP = 0.4    # protocol: at most every 400 ms
HEARTBEAT = 10.0      # protocol: heartbeat every 10 s


def ms(seconds):
    return int(seconds * 1000)


# -----------------------------------------------------------------------------
#  Transport
# -----------------------------------------------------------------------------
class Orb:
    def __init__(self, host, port, token, dry_run=False, verbose=False):
        self.base = "http://%s:%d" % (host, port)
        self.token = token
        self.dry_run = dry_run
        self.verbose = verbose
        self.failures = 0

    def _request(self, method, path, body=None):
        data = None if body is None else json.dumps(body, separators=(",", ":")).encode("utf-8")
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if data is not None:
            req.add_header("Content-Type", "application/json")
        if self.token:
            req.add_header("X-Orb-Token", self.token)
        with urllib.request.urlopen(req, timeout=3) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")

    def post(self, path, body):
        if self.dry_run:
            print(json.dumps(body, indent=None, separators=(",", ":")))
            return True
        try:
            status, text = self._request("POST", path, body)
            if self.verbose:
                print("  %s %s -> %d %s" % (path, body.get("sid"), status, text))
            if self.failures:
                print("  device reachable again")
            self.failures = 0
            return True
        except urllib.error.HTTPError as e:
            msg = e.read().decode("utf-8", "replace")
            if e.code == 401:
                sys.exit("device rejected the token (401). Pass --token matching the settings page.")
            print("  %s -> HTTP %d %s" % (path, e.code, msg), file=sys.stderr)
        except (urllib.error.URLError, OSError) as e:
            self.failures += 1
            if self.failures in (1, 5) or self.failures % 30 == 0:
                print("  cannot reach %s (%s) - retrying" % (self.base, getattr(e, "reason", e)),
                      file=sys.stderr)
        return False

    def info(self):
        status, text = self._request("GET", "/api/info")
        return json.loads(text)


# -----------------------------------------------------------------------------
#  Shared account-wide rate limits
# -----------------------------------------------------------------------------
class Limits:
    def __init__(self):
        self.t0 = time.monotonic()
        self.pct5h = random.uniform(15, 45)
        self.pct7d = random.uniform(5, 30)
        self.reset5h = random.uniform(1.5, 4.5) * 3600
        self.reset7d = random.uniform(2, 6) * 86400

    def tick(self, dt, busy_sessions):
        self.pct5h = min(100.0, self.pct5h + dt * 0.02 * busy_sessions)
        self.pct7d = min(100.0, self.pct7d + dt * 0.002 * busy_sessions)

    def payload(self):
        el = time.monotonic() - self.t0
        return [
            {"kind": "5h", "pct": round(self.pct5h, 1), "resetInMs": ms(max(0.0, self.reset5h - el))},
            {"kind": "7d", "pct": round(self.pct7d, 1), "resetInMs": ms(max(0.0, self.reset7d - el))},
        ]


# -----------------------------------------------------------------------------
#  A simulated Claude Code session, driven by a script of phases
# -----------------------------------------------------------------------------
class Phase:
    """state for `secs` seconds; optional effects applied when the phase starts."""

    def __init__(self, state, secs, tool="", detail="", **effects):
        self.state, self.secs, self.tool, self.detail, self.effects = state, secs, tool, detail, effects


class SimSession:
    def __init__(self, idx, limits, script_factory, project=None):
        self.sid = "sim%02d%04x" % (idx, random.randrange(0x10000))
        self.project = project or PROJECTS[idx % len(PROJECTS)]
        self.model, self.effort = MODELS[idx % len(MODELS)]
        self.mode = ["default", "acceptEdits", "plan", "default"][idx % 4]
        self.limits = limits
        self.window = 200000
        self.tokens = random.randint(18000, 60000)
        self.compactions = 0
        self.cost = round(random.uniform(0.05, 2.0), 3)
        self.gate = {"allowed": random.randint(5, 40), "asked": random.randint(0, 5), "denied": 0, "pending": 0}
        self.state, self.tool, self.detail = "idle", "", ""
        self.turn_running = False
        self.turn_start = 0.0
        self.turn_cost0 = self.cost
        self.steps = self.edits = self.errors = 0
        self.last = None
        self.agents = []          # dicts with name, task, status, ctx, steps, started, done_at
        self.log = []
        self.ttft = None
        self.script = script_factory(self)
        self.phase = None
        self.phase_left = 0.0
        self.version = 0          # bumps on every meaningful change
        self.sent_version = -1
        self.last_sent = 0.0

    # -- helpers used by scripts --
    def add_log(self, line):
        self.log = (self.log + [line[:32]])[-4:]

    def start_turn(self):
        self.turn_running = True
        self.turn_start = time.monotonic()
        self.turn_cost0 = self.cost
        self.steps = self.edits = self.errors = 0
        self.ttft = random.randint(450, 1800)

    def end_turn(self):
        self.last = {
            "durationMs": ms(time.monotonic() - self.turn_start),
            "costDelta": round(self.cost - self.turn_cost0, 3),
            "edits": self.edits,
            "errors": self.errors,
        }
        self.turn_running = False
        for a in self.agents:
            if a["status"] == "running":
                a["status"] = "done"
        self.agents = []

    # -- simulation --
    def _enter(self, ph):
        self.phase, self.phase_left = ph, ph.secs
        fx = ph.effects
        if fx.get("start_turn"):
            self.start_turn()
        prev = self.state
        self.state, self.tool, self.detail = ph.state, ph.tool, ph.detail
        if ph.state in ("tool", "error") and ph.tool:
            self.steps += 1
            verb = ph.tool.lower()
            self.add_log(("%s %s" % (verb, ph.detail))[:32])
            if ph.tool in ("Edit", "Write") and ph.state == "tool":
                self.edits += 1
        if ph.state == "error":
            self.errors += 1
        if ph.state == "waiting":
            self.gate["asked"] += 1
            self.gate["pending"] = 1
        elif prev == "waiting":
            self.gate["pending"] = 0
            self.gate["allowed"] += 1
        if ph.state == "compacting":
            self.compactions += 1
        for spec in fx.get("spawn", []):
            name, task, secs = spec
            self.agents.append({"name": name, "task": task, "status": "running", "ctx": random.randint(4000, 9000),
                                "steps": 0, "started": time.monotonic(), "runtime": secs, "done_at": None})
            self.add_log("spawn %s" % name)
        if fx.get("end_turn"):
            self.end_turn()
        self.version += 1

    def tick(self, dt):
        if self.phase is None or self.phase_left <= 0:
            try:
                self._enter(next(self.script))
            except StopIteration:
                return False
        self.phase_left -= dt
        if self.turn_running:
            # tokens + cost grow while working; compaction knocks tokens back down
            if self.state == "compacting":
                self.tokens = max(30000, int(self.tokens * (1 - 0.25 * dt)))
            else:
                self.tokens = min(self.window, self.tokens + int(random.uniform(300, 1400) * dt))
            self.cost = round(self.cost + random.uniform(0.002, 0.01) * dt * (2 if self.state == "tool" else 1), 4)
            now = time.monotonic()
            for a in self.agents:
                if a["status"] != "running":
                    continue
                if random.random() < 0.35 * dt * 3:
                    a["steps"] += 1
                    a["ctx"] += random.randint(500, 2500)
                    self.version += 1
                if now - a["started"] >= a["runtime"]:
                    a["status"] = "error" if random.random() < 0.12 else "done"
                    a["done_at"] = now
                    self.version += 1
        return True

    def busy(self):
        return self.turn_running or self.state != "idle"

    def payload(self):
        now = time.monotonic()
        agents_list = sorted(self.agents, key=lambda a: 0 if a["status"] == "running" else 1)[:4]
        return {
            "v": 1,
            "sid": self.sid,
            "project": self.project[:24],
            "state": self.state,
            "tool": self.tool,
            "detail": self.detail[:40],
            "model": self.model,
            "effort": self.effort,
            "mode": self.mode,
            "ctx": {"pct": round(100.0 * self.tokens / self.window, 1), "tokens": self.tokens,
                    "window": self.window, "compactions": self.compactions},
            "cost": round(self.cost, 3),
            "limits": self.limits.payload(),
            "turn": {"running": self.turn_running,
                     "elapsedMs": ms(now - self.turn_start) if self.turn_running else 0,
                     "steps": self.steps, "edits": self.edits, "errors": self.errors},
            "last": self.last,
            "agents": {
                "running": sum(1 for a in self.agents if a["status"] == "running"),
                "total": len(self.agents),
                "list": [{"name": a["name"], "task": a["task"][:28], "status": a["status"], "ctx": a["ctx"],
                          "steps": a["steps"],
                          "elapsedMs": ms((a["done_at"] or now) - a["started"])} for a in agents_list],
            },
            "gate": dict(self.gate),
            "ttftMs": self.ttft,
            "log": self.log,
        }


# -----------------------------------------------------------------------------
#  Scripts (generators of Phase)
# -----------------------------------------------------------------------------
def rand_tool():
    return random.choice(TOOLS)


def script_thinking(s):
    while True:
        yield Phase("idle", 3)
        yield Phase("thinking", random.uniform(18, 30), start_turn=True)
        yield Phase("tool", 3, "Read", "src/core/session.ts")
        yield Phase("thinking", random.uniform(8, 14))
        yield Phase("idle", 6, end_turn=True)


def script_tools(s):
    while True:
        yield Phase("idle", 3)
        yield Phase("thinking", random.uniform(2, 4), start_turn=True)
        for i in range(random.randint(6, 10)):
            tool, detail = rand_tool()
            if random.random() < 0.12:
                yield Phase("error", 2.5, tool, detail)
            else:
                yield Phase("tool", random.uniform(1.5, 4.5), tool, detail)
            yield Phase("thinking", random.uniform(1.5, 3.5))
            if s.tokens > 0.82 * s.window:
                yield Phase("compacting", 5)
        yield Phase("idle", 6, end_turn=True)


def script_agents(s):
    while True:
        yield Phase("idle", 3)
        yield Phase("thinking", 3, start_turn=True)
        specs = [(n, t, random.uniform(10, 28)) for n, t in random.sample(AGENT_TASKS, random.randint(2, 4))]
        yield Phase("tool", 4, "Task", "%s: %s" % (specs[0][0], specs[0][1]), spawn=specs)
        yield Phase("tool", 26, "Task", "waiting on %d agents" % len(specs))
        yield Phase("thinking", 5)
        yield Phase("tool", 3, "Edit", "src/core/hooks.ts")
        yield Phase("idle", 6, end_turn=True)


def script_waiting(s):
    while True:
        yield Phase("idle", 3)
        yield Phase("thinking", 4, start_turn=True)
        yield Phase("tool", 3, "Read", "package.json")
        yield Phase("waiting", 14, "Bash", "rm -rf build && npm run build")
        yield Phase("tool", 4, "Bash", "rm -rf build && npm run build")
        yield Phase("thinking", 4)
        yield Phase("idle", 8, end_turn=True)


def script_mixed(s):
    """multi: each session runs a varied mix, occasionally asking for permission."""
    yield Phase("idle", random.uniform(0, 6))
    while True:
        yield Phase("thinking", random.uniform(2, 5), start_turn=True)
        for i in range(random.randint(4, 8)):
            tool, detail = rand_tool()
            r = random.random()
            if r < 0.08:
                yield Phase("waiting", random.uniform(6, 10), tool, detail)
            if r > 0.93:
                yield Phase("error", 2, tool, detail)
            else:
                yield Phase("tool", random.uniform(2, 5), tool, detail)
            yield Phase("thinking", random.uniform(2, 4))
        yield Phase("idle", random.uniform(8, 20), end_turn=True)


SCRIPTS = {
    "thinking": script_thinking,
    "tools": script_tools,
    "agents": script_agents,
    "waiting": script_waiting,
    "multi": script_mixed,
}


# -----------------------------------------------------------------------------
#  Runner
# -----------------------------------------------------------------------------
def make_sessions(scenario, count, limits):
    if scenario == "multi":
        count = max(count, 2)
    script = SCRIPTS[scenario]
    return [SimSession(i, limits, script) for i in range(count)]


def end_all(orb, sessions):
    for s in sessions:
        orb.post("/api/end", {"v": 1, "sid": s.sid})


def run_scenario(orb, scenario, count, duration, tick, limits, quiet):
    # In --dry-run, stdout carries only JSON lines; human output goes to stderr.
    out = sys.stderr if orb.dry_run else sys.stdout
    sessions = make_sessions(scenario, count, limits)
    label = "%s x%d" % (scenario, len(sessions))
    print("scenario %s%s" % (label, "" if duration is None else " for %ds" % duration), file=out)
    started = time.monotonic()
    last = started
    states = {}
    try:
        while duration is None or time.monotonic() - started < duration:
            now = time.monotonic()
            dt, last = now - last, now
            busy = 0
            for s in sessions:
                s.tick(dt)
                busy += 1 if s.busy() else 0
            limits.tick(dt, busy)
            for s in sessions:
                changed = s.version != s.sent_version
                due = now - s.last_sent >= HEARTBEAT
                if (changed and now - s.last_sent >= MIN_SEND_GAP) or due:
                    if orb.post("/api/state", s.payload()):
                        s.sent_version = s.version
                    s.last_sent = now
                if not quiet and states.get(s.sid) != (s.state, s.tool):
                    states[s.sid] = (s.state, s.tool)
                    print("  %-12s %-10s %s %s" % (s.project, s.state, s.tool, s.detail), file=out)
            time.sleep(tick)
    finally:
        end_all(orb, sessions)


def main(argv=None):
    p = argparse.ArgumentParser(description="Push fake Claude Code sessions to a Claude Orb (protocol v1).")
    p.add_argument("--host", default="claude-orb.local", help="device host or IP (default claude-orb.local)")
    p.add_argument("--port", type=int, default=80, help="HTTP port (default 80)")
    p.add_argument("--token", default="", help="X-Orb-Token, if one is set on the device")
    p.add_argument("--scenario", choices=SCENARIOS, default="cycle",
                   help="what to simulate (cycle walks through all of them)")
    p.add_argument("--sessions", type=int, default=1, help="number of sessions (multi: default 3)")
    p.add_argument("--duration", type=float, default=None,
                   help="seconds to run (default: forever; cycle: 45 s per scenario)")
    p.add_argument("--tick", type=float, default=0.2, help="simulation step in seconds (default 0.2)")
    p.add_argument("--seed", type=int, default=None, help="random seed for repeatable runs")
    p.add_argument("--info", action="store_true", help="print GET /api/info and exit")
    p.add_argument("--dry-run", action="store_true", help="print payloads instead of sending")
    p.add_argument("-v", "--verbose", action="store_true", help="log every HTTP response")
    p.add_argument("-q", "--quiet", action="store_true", help="do not print state changes")
    args = p.parse_args(argv)

    if args.sessions < 1 or args.sessions > 8:
        p.error("--sessions must be 1..8 (the device holds 4)")
    if args.tick <= 0:
        p.error("--tick must be > 0")
    if args.seed is not None:
        random.seed(args.seed)

    orb = Orb(args.host, args.port, args.token, dry_run=args.dry_run, verbose=args.verbose)

    if args.info:
        try:
            print(json.dumps(orb.info(), indent=2))
        except urllib.error.HTTPError as e:
            sys.exit("HTTP %d: %s" % (e.code, e.read().decode("utf-8", "replace")))
        except (urllib.error.URLError, OSError) as e:
            sys.exit("cannot reach %s: %s" % (orb.base, getattr(e, "reason", e)))
        return 0

    limits = Limits()
    try:
        if args.scenario == "cycle":
            per = args.duration or 45
            while True:
                for sc in ("thinking", "tools", "agents", "waiting", "multi"):
                    n = 3 if sc == "multi" and args.sessions == 1 else args.sessions
                    run_scenario(orb, sc, n, per, args.tick, limits, args.quiet)
        else:
            n = args.sessions
            if args.scenario == "multi" and n == 1:
                n = 3
            run_scenario(orb, args.scenario, n, args.duration, args.tick, limits, args.quiet)
    except KeyboardInterrupt:
        print("\nstopped - sessions ended on the device", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
