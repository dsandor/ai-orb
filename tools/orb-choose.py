#!/usr/bin/env python3
"""Find Desk Orbs on the LAN and pick the one OpenCode reports to.

Writes ~/.config/deskorb/device.json, which the OpenCode plugin (opencode/deskorb.ts) re-reads every
10 s. (Claude Code keeps its own pick: use /orb choose there.)

  python3 tools/orb-choose.py              # scan, then pick from a list
  python3 tools/orb-choose.py --list       # scan and print, change nothing
  python3 tools/orb-choose.py --url 192.168.1.50 [--token T]
  python3 tools/orb-choose.py --reset      # forget the pick
"""
import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path

CHOICE = Path.home() / ".config" / "deskorb" / "device.json"


def unescape(s):
    return re.sub(r"\\(\d{3})|\\(.)", lambda m: chr(int(m[1])) if m[1] else m[2], s)


def txt_of(s):
    out = {}
    for raw in re.findall(r'"((?:[^"\\]|\\.)*)"', s):
        k, eq, v = unescape(raw).partition("=")
        if eq:
            out[k.lower()] = v
    return out


def browse():
    """(type, host, port, ip, txt) records from dns-sd (macOS) or avahi-browse (Linux)."""
    found = {}
    if shutil.which("dns-sd"):
        out = ""
        for svc in ("_claude-orb._tcp", "_http._tcp"):
            try:
                out += subprocess.run(["dns-sd", "-Z", svc, "local"], capture_output=True, text=True, timeout=2).stdout
            except subprocess.TimeoutExpired as e:  # dns-sd never exits by itself
                out += (e.stdout or b"").decode() if isinstance(e.stdout, bytes) else (e.stdout or "")
        for line in out.splitlines():
            m = re.match(r"^(\S+?\.(_claude-orb|_http)\._tcp)\s+(SRV|TXT)\s+(.*)$", line)
            if not m:
                continue
            f = found.setdefault(m[1], {"type": m[2], "host": "", "port": 80, "ip": "", "txt": {}})
            if m[3] == "SRV":
                srv = re.match(r"^\d+\s+\d+\s+(\d+)\s+(\S+)", m[4])
                if srv:
                    f["port"], f["host"] = int(srv[1]), srv[2].rstrip(".")
            else:
                f["txt"].update(txt_of(m[4]))
    elif shutil.which("avahi-browse"):
        for svc in ("_claude-orb._tcp", "_http._tcp"):
            out = subprocess.run(["avahi-browse", "-rpt", svc], capture_output=True, text=True, timeout=10).stdout
            for line in out.splitlines():
                a = line.split(";")
                if a[0] == "=" and len(a) >= 9 and a[2] == "IPv4":
                    found[f"{a[3]}.{a[4]}"] = {"type": a[4].split(".")[0], "host": a[6].rstrip("."), "port": int(a[8] or 80),
                                               "ip": a[7], "txt": txt_of(";".join(a[9:]))}
    else:
        raise SystemExit("No mDNS browser found (needs dns-sd or avahi-browse). Use --url <IP>.")
    return found.values()


def is_lan_ip(ip):
    m = re.fullmatch(r"(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})", ip or "")
    if not m or any(int(n) > 255 for n in m.groups()):
        return False
    a, b = int(m[1]), int(m[2])
    return a == 10 or (a == 172 and 16 <= b <= 31) or (a == 192 and b == 168) or (a == 169 and b == 254)


def is_host_name(h):
    return len(h or "") <= 253 and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9-]{0,62}(\.[A-Za-z0-9][A-Za-z0-9-]{0,62})*", h or "") is not None


def devices():
    out = {}
    for f in sorted(browse(), key=lambda f: f["type"] == "_http"):  # _claude-orb records first
        t = f["txt"]
        if f["type"] == "_http" and t.get("api") != "/api/state":
            continue
        host = f["host"] if is_host_name(f["host"]) else ""
        ip = next((x for x in (t.get("ip", ""), f["ip"]) if is_lan_ip(x)), "")
        if not host and not ip:
            continue
        name = next((x for x in (t.get("name", ""), re.sub(r"\.local$", "", host, flags=re.I)) if is_host_name(x)), ip)
        port = f["port"] if 0 < f["port"] < 65536 else 80
        d = {"name": name, "host": host, "ip": ip, "port": port, "fw": re.sub(r"[^\x20-\x7e]", "", t.get("fw", ""))[:16]}
        out.setdefault(f"{(d['ip'] or d['host']).lower()}:{d['port']}", d)
    return sorted(out.values(), key=lambda d: (d["name"], d["ip"]))


def url_of(d):
    port = f":{d['port']}" if d["port"] not in (0, 80) else ""
    own = d["host"] and d["host"].lower() == f"{d['name'].lower()}.local"  # not renamed by an mDNS clash
    return f"http://{d['host'] if own or not d['ip'] else d['ip']}{port}"


def base_url(u):
    u = u.strip().rstrip("/")
    return u if re.match(r"^https?://", u, re.I) else f"http://{u}"


def current():
    try:
        return json.loads(CHOICE.read_text()).get("url", "")
    except (OSError, ValueError):
        return ""


def save(url, token):
    CHOICE.parent.mkdir(parents=True, exist_ok=True)
    CHOICE.write_text(json.dumps({"url": url, **({"token": token} if token else {})}, indent=2) + "\n")
    print(f"OpenCode sessions now report to {url} (saved in {CHOICE}).")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--list", action="store_true", help="scan and print the orbs found")
    ap.add_argument("--url", help="use this orb (name.local, IP or URL) without scanning")
    ap.add_argument("--token", default="", help="the orb's API token, if it has one")
    ap.add_argument("--reset", action="store_true", help="forget the pick")
    a = ap.parse_args()

    if a.reset:
        CHOICE.unlink(missing_ok=True)
        print("Forgot the chosen orb.")
        return
    if a.url:
        save(base_url(a.url), a.token)
        return

    ds = devices()
    if not ds:
        raise SystemExit("No Desk Orbs found. Is it on this network and on firmware with _claude-orb mDNS? Try --url <IP>.")
    cur = current()
    for i, d in enumerate(ds, 1):
        mark = "*" if url_of(d) == cur else " "
        print(f"{mark} {i}. {d['name']:<20} {url_of(d):<28} {('fw ' + d['fw']) if d['fw'] else ''}")
    if a.list:
        return
    pick = input(f"Which orb should OpenCode report to? [1-{len(ds)}] ").strip()
    if not pick.isdigit() or not 1 <= int(pick) <= len(ds):
        raise SystemExit("Nothing changed.")
    save(url_of(ds[int(pick) - 1]), a.token)


if __name__ == "__main__":
    main()
