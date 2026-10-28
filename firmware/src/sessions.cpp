// =============================================================================
//  Session table - see docs/protocol.md
// =============================================================================
#include "sessions.h"
#include <ArduinoJson.h>
#include <math.h>

static Session       table[ORB_MAX_SESSIONS];
static SessionEvents events;
static uint32_t      lastChangeMs = 0;

// -----------------------------------------------------------------------------
//  Defensive JSON helpers
// -----------------------------------------------------------------------------

// Copies a JSON string into a fixed buffer: never splits a UTF-8 sequence,
// replaces control characters with spaces, accepts null/missing/non-strings
// as "".
static void copyStr(char* dst, size_t cap, JsonVariantConst v) {
  dst[0] = '\0';
  if (!v.is<const char*>()) return;
  const char* src = v.as<const char*>();
  if (!src) return;
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)src;
  while (*p) {
    uint8_t c = *p;
    size_t n = 1;
    if (c >= 0xF0) n = 4;
    else if (c >= 0xE0) n = 3;
    else if (c >= 0xC0) n = 2;
    else if (c >= 0x80) { p++; continue; }  // stray continuation byte
    if (o + n > cap - 1) break;
    bool ok = true;
    for (size_t i = 1; i < n; i++) {
      if ((p[i] & 0xC0) != 0x80) { ok = false; break; }
    }
    if (!ok) { p++; continue; }
    if (n == 1 && c < 0x20) c = ' ';
    dst[o++] = (char)c;
    for (size_t i = 1; i < n; i++) dst[o++] = (char)p[i];
    p += n;
  }
  dst[o] = '\0';
}

static float numF(JsonVariantConst v, float def) {
  if (!v.is<float>()) return def;
  float f = v.as<float>();
  return isfinite(f) ? f : def;
}

static uint32_t numU(JsonVariantConst v, uint32_t def, uint32_t maxv) {
  if (!v.is<double>()) return def;
  double d = v.as<double>();
  if (!isfinite(d) || d < 0) return def;
  if (d > (double)maxv) return maxv;
  return (uint32_t)d;
}

static int64_t numMs(JsonVariantConst v) {   // -1 when null / missing / bad
  if (!v.is<double>()) return -1;
  double d = v.as<double>();
  if (!isfinite(d) || d < 0) return -1;
  if (d > 4.0e12) d = 4.0e12;
  return (int64_t)d;
}

static SState parseState(const char* s, bool running) {
  if (s) {
    if (!strcmp(s, "idle"))       return SState::Idle;
    if (!strcmp(s, "thinking"))   return SState::Thinking;
    if (!strcmp(s, "tool"))       return SState::Tool;
    if (!strcmp(s, "waiting"))    return SState::Waiting;
    if (!strcmp(s, "error"))      return SState::Error;
    if (!strcmp(s, "compacting")) return SState::Compacting;
  }
  return running ? SState::Thinking : SState::Idle;
}

static AStatus parseAStatus(const char* s) {
  if (s && !strcmp(s, "done"))  return AStatus::Done;
  if (s && !strcmp(s, "error")) return AStatus::Error;
  return AStatus::Running;
}

const char* sstateName(SState s) {
  switch (s) {
    case SState::Idle:       return "idle";
    case SState::Thinking:   return "thinking";
    case SState::Tool:       return "tool";
    case SState::Waiting:    return "waiting";
    case SState::Error:      return "error";
    case SState::Compacting: return "compacting";
  }
  return "?";
}

// -----------------------------------------------------------------------------
//  Table
// -----------------------------------------------------------------------------
void sessionsBegin() {
  memset(table, 0, sizeof(table));
}

int sessionFind(const char* sid) {
  if (!sid || !sid[0]) return -1;
  for (int i = 0; i < ORB_MAX_SESSIONS; i++) {
    if (table[i].used && !strcmp(table[i].sid, sid)) return i;
  }
  return -1;
}

static int allocSlot() {
  for (int i = 0; i < ORB_MAX_SESSIONS; i++) {
    if (!table[i].used) return i;
  }
  // Full: evict the session heard from least recently.
  int victim = 0;
  for (int i = 1; i < ORB_MAX_SESSIONS; i++) {
    if ((int32_t)(table[i].rxMs - table[victim].rxMs) < 0) victim = i;
  }
  return victim;
}

// Cheap fingerprint of the fields that count as "the session changed".
static uint32_t fingerprint(const Session& s) {
  uint32_t h = 2166136261u;
  auto mix = [&h](const void* d, size_t n) {
    const uint8_t* p = (const uint8_t*)d;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
  };
  mix(&s.state, sizeof(s.state));
  mix(s.tool, strlen(s.tool));
  mix(s.detail, strlen(s.detail));
  mix(&s.turnSteps, sizeof(s.turnSteps));
  mix(&s.turnRunning, sizeof(s.turnRunning));
  mix(&s.agentsRunning, sizeof(s.agentsRunning));
  mix(&s.gatePending, sizeof(s.gatePending));
  return h;
}

int sessionsApplyState(const char* body, size_t len, const char*& err) {
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, body, len,
                                            DeserializationOption::NestingLimit(6));
  if (de) { err = "bad json"; return 400; }
  if (!doc.is<JsonObjectConst>()) { err = "expected object"; return 400; }
  JsonObjectConst o = doc.as<JsonObjectConst>();

  static Session n;   // static: keeps ~1 KB off the loop-task stack
  memset(&n, 0, sizeof(n));
  copyStr(n.sid, sizeof(n.sid), o["sid"]);
  if (!n.sid[0]) { err = "missing sid"; return 400; }

  JsonObjectConst turn = o["turn"];
  n.turnRunning   = turn["running"].is<bool>() && turn["running"].as<bool>();
  n.turnElapsedMs = numU(turn["elapsedMs"], 0, 0xFFFFFFFFu);
  n.turnSteps     = numU(turn["steps"], 0, 65535);
  n.turnEdits     = numU(turn["edits"], 0, 65535);
  n.turnErrors    = numU(turn["errors"], 0, 65535);

  n.state = parseState(o["state"].as<const char*>(), n.turnRunning);
  copyStr(n.project, sizeof(n.project), o["project"]);
  if (!n.project[0]) strlcpy(n.project, "claude", sizeof(n.project));
  copyStr(n.tool,   sizeof(n.tool),   o["tool"]);
  copyStr(n.detail, sizeof(n.detail), o["detail"]);
  copyStr(n.model,  sizeof(n.model),  o["model"]);
  copyStr(n.effort, sizeof(n.effort), o["effort"]);
  copyStr(n.mode,   sizeof(n.mode),   o["mode"]);

  JsonObjectConst ctx = o["ctx"];
  n.ctxPct      = numF(ctx["pct"], NAN);
  if (!isnan(n.ctxPct)) n.ctxPct = constrain(n.ctxPct, 0.0f, 100.0f);
  n.ctxTokens   = numU(ctx["tokens"], 0, 0xFFFFFFFFu);
  n.ctxWindow   = numU(ctx["window"], 0, 0xFFFFFFFFu);
  n.compactions = numU(ctx["compactions"], 0, 999);
  n.cost        = numF(o["cost"], NAN);
  if (!isnan(n.cost) && n.cost < 0) n.cost = NAN;

  n.lim5h.resetInMs = n.lim7d.resetInMs = -1;
  JsonArrayConst limits = o["limits"];
  for (JsonObjectConst l : limits) {
    const char* kind = l["kind"];
    LimitInfo* dst = nullptr;
    if (kind && !strcmp(kind, "5h")) dst = &n.lim5h;
    else if (kind && !strcmp(kind, "7d")) dst = &n.lim7d;
    if (!dst) continue;
    float pct = numF(l["pct"], NAN);
    if (isnan(pct)) continue;
    dst->present   = true;
    dst->pct       = constrain(pct, 0.0f, 999.0f);
    dst->resetInMs = numMs(l["resetInMs"]);
  }

  JsonObjectConst last = o["last"];
  if (!last.isNull()) {
    n.hasLast        = true;
    n.lastDurationMs = numU(last["durationMs"], 0, 0xFFFFFFFFu);
    n.lastCostDelta  = numF(last["costDelta"], NAN);
    n.lastEdits      = numU(last["edits"], 0, 65535);
    n.lastErrors     = numU(last["errors"], 0, 65535);
  }

  JsonObjectConst ag = o["agents"];
  n.agentsRunning = numU(ag["running"], 0, 255);
  n.agentsTotal   = numU(ag["total"], 0, 255);
  for (JsonVariantConst av : ag["list"].as<JsonArrayConst>()) {
    if (n.agentCount >= ORB_MAX_AGENTS) break;
    if (!av.is<JsonObjectConst>()) continue;   // skip junk entries
    JsonObjectConst a = av.as<JsonObjectConst>();
    AgentInfo& d = n.agents[n.agentCount++];
    copyStr(d.name, sizeof(d.name), a["name"]);
    if (!d.name[0]) strlcpy(d.name, "agent", sizeof(d.name));
    copyStr(d.task, sizeof(d.task), a["task"]);
    d.status    = parseAStatus(a["status"].as<const char*>());
    d.ctx       = numU(a["ctx"], 0, 0xFFFFFFFFu);
    d.steps     = numU(a["steps"], 0, 65535);
    d.elapsedMs = numU(a["elapsedMs"], 0, 0xFFFFFFFFu);
  }
  if (n.agentsTotal < n.agentCount) n.agentsTotal = n.agentCount;

  JsonObjectConst gate = o["gate"];
  n.gateAllowed = numU(gate["allowed"], 0, 65535);
  n.gateAsked   = numU(gate["asked"], 0, 65535);
  n.gateDenied  = numU(gate["denied"], 0, 65535);
  n.gatePending = numU(gate["pending"], 0, 65535);
  n.ttftMs      = o["ttftMs"].is<double>() ? (int32_t)numU(o["ttftMs"], 0, 3600000) : -1;

  for (JsonVariantConst l : o["log"].as<JsonArrayConst>()) {
    if (n.logCount >= ORB_MAX_LOG) break;
    copyStr(n.log[n.logCount], sizeof(n.log[0]), l);
    if (n.log[n.logCount][0]) n.logCount++;
  }

  // ---- merge into table ----
  const uint32_t now = millis();
  int idx = sessionFind(n.sid);
  bool isNew = idx < 0;
  if (isNew) idx = allocSlot();
  Session& s = table[idx];

  bool wasBusy = !isNew && (s.state != SState::Idle || s.turnRunning);
  uint32_t oldFp = isNew ? 0 : fingerprint(s);
  SState oldState = isNew ? SState::Idle : s.state;
  uint32_t changed = isNew ? now : s.changedMs;
  uint32_t since   = isNew ? now : s.stateSinceMs;
  uint32_t doneAt  = isNew ? 0 : s.doneAtMs;

  n.used = true;
  n.rxMs = now;
  uint32_t newFp = fingerprint(n);
  if (isNew || newFp != oldFp) changed = now;
  if (!isNew && oldState != n.state) since = now;
  n.changedMs    = changed;
  n.stateSinceMs = since;
  n.doneAtMs     = doneAt;

  bool nowIdle = n.state == SState::Idle && !n.turnRunning;
  if (wasBusy && nowIdle) {
    n.doneAtMs = now ? now : 1;
    events.turnDoneIdx = idx;
  }
  s = n;
  if (changed == now) lastChangeMs = now;
  return 200;
}

int sessionsApplyEnd(const char* body, size_t len, const char*& err) {
  JsonDocument doc;
  if (deserializeJson(doc, body, len, DeserializationOption::NestingLimit(4))) {
    err = "bad json";
    return 400;
  }
  char sid[24];
  copyStr(sid, sizeof(sid), doc["sid"]);
  if (!sid[0]) { err = "missing sid"; return 400; }
  int idx = sessionFind(sid);
  if (idx >= 0) {
    table[idx].used = false;
    if (events.turnDoneIdx == idx) events.turnDoneIdx = -1;
    lastChangeMs = millis();
  }
  return 200;
}

// A loop that read millis() before handling requests holds a `now` older than a session it just
// received; unsigned `now - rxMs` would wrap to ~49 days and drop it. Such a session is age 0.
uint32_t sessionAge(const Session& s, uint32_t now) {
  int32_t d = (int32_t)(now - s.rxMs);
  return d > 0 ? (uint32_t)d : 0;
}

void sessionsTick(uint32_t now) {
  for (int i = 0; i < ORB_MAX_SESSIONS; i++) {
    if (table[i].used && sessionAge(table[i], now) > ORB_DROP_MS) {
      table[i].used = false;
      if (events.turnDoneIdx == i) events.turnDoneIdx = -1;
    }
  }
}

uint8_t sessionsCount() {
  uint8_t n = 0;
  for (auto& s : table) n += s.used ? 1 : 0;
  return n;
}

Session* sessionAt(uint8_t idx) {
  return (idx < ORB_MAX_SESSIONS && table[idx].used) ? &table[idx] : nullptr;
}

SessionEvents& sessionEvents() { return events; }
uint32_t sessionsLastChangeMs() { return lastChangeMs; }

bool sessionIsStale(const Session& s, uint32_t now) {
  return sessionAge(s, now) > ORB_STALE_MS;
}

bool sessionIsBusy(const Session& s, uint32_t now) {
  return s.used && !sessionIsStale(s, now) && (s.state != SState::Idle || s.turnRunning);
}

uint8_t sessionsBusyCount(uint32_t now) {
  uint8_t n = 0;
  for (auto& s : table) n += sessionIsBusy(s, now) ? 1 : 0;
  return n;
}

// Rotating pick among the slots matching `pred`: keeps the current pick for
// ORB_ROTATE_MS, starts at the most recently changed one.
template <typename Pred>
static int rotatingPick(int& cur, uint32_t& since, uint32_t now, Pred pred, uint8_t* countOut) {
  int count = 0, newest = -1;
  for (int i = 0; i < ORB_MAX_SESSIONS; i++) {
    if (!pred(table[i])) continue;
    count++;
    if (newest < 0 || (int32_t)(table[i].changedMs - table[newest].changedMs) > 0) newest = i;
  }
  if (countOut) *countOut = count;
  if (count == 0) { cur = -1; return -1; }
  bool curOk = cur >= 0 && cur < ORB_MAX_SESSIONS && pred(table[cur]);
  if (!curOk) {
    cur = newest;
    since = now;
  } else if (count > 1 && now - since >= ORB_ROTATE_MS) {
    for (int k = 1; k <= ORB_MAX_SESSIONS; k++) {
      int j = (cur + k) % ORB_MAX_SESSIONS;
      if (pred(table[j])) { cur = j; break; }
    }
    since = now;
  }
  return cur;
}

int sessionsAttention(uint32_t now) {
  static int cur = -1;
  static uint32_t since = 0;
  return rotatingPick(cur, since, now, [now](const Session& s) {
    return s.used && s.state == SState::Waiting && !sessionIsStale(s, now);
  }, nullptr);
}

int sessionsFocus(uint32_t now, bool* multiBusy) {
  static int cur = -1;
  static uint32_t since = 0;
  uint8_t busy = 0;
  int f = rotatingPick(cur, since, now, [now](const Session& s) {
    return sessionIsBusy(s, now);
  }, &busy);
  if (multiBusy) *multiBusy = busy > 1;
  if (f >= 0) return f;
  // Nothing busy: most recently changed session, fresh ones first.
  int best = -1;
  for (int i = 0; i < ORB_MAX_SESSIONS; i++) {
    if (!table[i].used) continue;
    if (best < 0) { best = i; continue; }
    bool fi = !sessionIsStale(table[i], now), fb = !sessionIsStale(table[best], now);
    if (fi != fb) { if (fi) best = i; continue; }
    if ((int32_t)(table[i].changedMs - table[best].changedMs) > 0) best = i;
  }
  return best;
}

uint32_t sessionTurnElapsed(const Session& s, uint32_t now) {
  if (!s.turnRunning) return s.turnElapsedMs;
  uint32_t dt = sessionAge(s, now);
  if (dt > ORB_DROP_MS) dt = ORB_DROP_MS;   // don't run away if the host vanished
  return s.turnElapsedMs + dt;
}

int64_t sessionResetIn(const Session& s, const LimitInfo& l, uint32_t now) {
  if (!l.present || l.resetInMs < 0) return -1;
  int64_t r = l.resetInMs - (int64_t)sessionAge(s, now);
  return r < 0 ? 0 : r;
}

uint32_t sessionAgentElapsed(const Session& s, const AgentInfo& a, uint32_t now) {
  if (a.status != AStatus::Running) return a.elapsedMs;
  uint32_t dt = sessionAge(s, now);
  if (dt > ORB_DROP_MS) dt = ORB_DROP_MS;
  return a.elapsedMs + dt;
}
