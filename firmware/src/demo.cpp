// =============================================================================
//  Demo mode (build with -D ORB_DEMO=1, env esp32s3_round_demo).
//  Plays a scripted ~110 s loop through every screen with made-up sessions,
//  fed through the same parser as real pushes. Real pushes are ignored while
//  it runs (web.cpp), so a Claude Code session nearby can't interrupt it.
//
//  Loop (seconds):
//    0-4    idle clock
//    4-62   one session: thinking, tools, subagents, limits, needs-you, done
//    62-105 three sessions at once: compacting, error, needs-you, done
//    105-   sessions end, back to the clock
// =============================================================================
#include "demo.h"

#if ORB_DEMO
#include <ArduinoJson.h>
#include "sessions.h"
#include "ui.h"

namespace {

constexpr uint32_t LOOP_MS = 112000;
constexpr uint32_t PUSH_MS = 500;

struct Agent {
  const char* name;
  const char* task;
  const char* status;
  uint32_t    ctx;
  int         steps;
  uint32_t    elapsedMs;
};

struct Mock {
  const char* sid;
  const char* project;
  const char* model;
  const char* effort;
  const char* mode;
  const char* state = "idle";
  const char* tool = "";
  const char* detail = "";
  float       ctxPct = 0;
  uint32_t    window = 200000;
  int         compactions = 0;
  float       cost = 0;
  float       l5 = 0, l7 = 0;
  uint32_t    r5 = 0, r7 = 0;
  bool        running = false;
  uint32_t    elapsedMs = 0;
  int         steps = 0, edits = 0, errors = 0;
  bool        hasLast = false;
  uint32_t    lastMs = 0;
  float       lastCost = 0;
  int         lastEdits = 0, lastErrors = 0;
  int         agentsRunning = 0, agentsTotal = 0, nAgents = 0;
  Agent       agents[3];
  int         allowed = 0, asked = 0, denied = 0, pending = 0;
  int         ttftMs = 0;
  const char* log[4];
  int         nLog = 0;
};

struct Step {
  const char* tool;
  const char* detail;
  const char* log;
  bool        edit;
};

// Session 1's tool run, one every 2 s from t=15.
const Step S1_STEPS[] = {
  {"Read", "firmware/src/ui.cpp", "read ui.cpp", false},
  {"Grep", "drawAttention", "grep drawAttention", false},
  {"Edit", "firmware/src/ui_session.cpp", "edit ui_session.cpp", true},
  {"Read", "docs/protocol.md", "read protocol.md", false},
  {"Edit", "firmware/src/demo.cpp", "edit demo.cpp", true},
  {"Bash", "pio run -e esp32s3_round", "bash: pio run", false},
};
constexpr int S1_NSTEPS = sizeof(S1_STEPS) / sizeof(S1_STEPS[0]);

uint32_t epoch = 0;
uint32_t lastPush = 0;

float lerp(float a, float b, float t0, float t1, float t) {
  if (t <= t0) return a;
  if (t >= t1) return b;
  return a + (b - a) * (t - t0) / (t1 - t0);
}

uint32_t secs(float s) { return s > 0 ? (uint32_t)(s * 1000) : 0; }

void addLog(Mock& m, const char* line) {
  if (m.nLog == 4) {
    for (int i = 1; i < 4; i++) m.log[i - 1] = m.log[i];
    m.nLog = 3;
  }
  m.log[m.nLog++] = line;
}

void limits(Mock& m, float t) {
  m.l5 = lerp(34, 47, 0, 100, t);
  m.r5 = 8040000 - secs(t);           // 2 h 14 m
  m.l7 = lerp(18, 20, 0, 100, t);
  m.r7 = 280800000 - secs(t);         // 3 d 6 h
}

// ---- session 1: the full single-session story ---------------------------------------
bool session1(Mock& m, float t) {
  m.sid = "demo-s1"; m.project = "ai-orb"; m.model = "Opus 5.5"; m.effort = "high"; m.mode = "default";
  m.compactions = 1;
  m.ttftMs = 840;
  limits(m, t);
  if (t < 4) return false;

  m.allowed = 24;
  m.asked = 2;
  m.denied = 1;
  m.ctxPct = lerp(38, 61, 4, 62, t);
  m.cost = lerp(3.12f, 3.96f, 4, 62, t);

  if (t >= 62) {   // second turn, while the other sessions run
    m.hasLast = true; m.lastMs = 55000; m.lastCost = 0.84f; m.lastEdits = 2; m.lastErrors = 0;
    m.allowed = 26; m.asked = 3;
    m.ctxPct = lerp(61, 66, 62, 101, t);
    m.cost = lerp(3.96f, 4.31f, 62, 101, t);
    addLog(m, "edit demo.cpp");
    addLog(m, "bash: pio run");
    addLog(m, "bash: git push");
    if (t >= 66 && t < 101) {
      m.running = true;
      m.elapsedMs = secs(t - 66);
      m.steps = (int)((t - 66) / 4);
      m.state = (t < 72) ? "thinking" : "tool";
      m.tool = "Read";
      m.detail = "firmware/src/ui_agents.cpp";
      if (t >= 82) { m.tool = "Edit"; m.detail = "firmware/src/ui_agents.cpp"; m.edits = 1; addLog(m, "read ui_agents.cpp"); }
    } else if (t >= 101) {
      m.hasLast = true; m.lastMs = 35000; m.lastCost = 0.35f; m.lastEdits = 1; m.lastErrors = 0;
      addLog(m, "edit ui_agents.cpp");
    }
    return t < 105;
  }

  m.hasLast = true; m.lastMs = 41000; m.lastCost = 0.37f; m.lastEdits = 2; m.lastErrors = 0;
  addLog(m, "spawn Explore");
  addLog(m, "bash: npm test");

  m.running = t < 59;
  m.elapsedMs = secs(t - 4);
  if (t < 15) {
    m.state = "thinking";
    m.steps = t < 10 ? 0 : 1;
  } else if (t < 27) {
    int i = (int)((t - 15) / 2);
    if (i >= S1_NSTEPS) i = S1_NSTEPS - 1;
    m.state = "tool";
    m.tool = S1_STEPS[i].tool;
    m.detail = S1_STEPS[i].detail;
    m.steps = 2 + i;
    for (int k = 0; k <= i; k++) {
      if (S1_STEPS[k].edit) m.edits++;
      if (k < i) addLog(m, S1_STEPS[k].log);
    }
  } else {
    m.steps = 8;
    m.edits = 2;
    for (int k = 0; k < S1_NSTEPS; k++) addLog(m, S1_STEPS[k].log);
  }

  // Subagents from t=27.
  if (t >= 27) {
    const float a = t - 27;
    m.agentsTotal = 3;
    m.nAgents = 3;
    m.agents[0] = {"Explore", "map the render pipeline", a < 7 ? "running" : "done",
                   (uint32_t)lerp(4000, 31000, 0, 7, a), (int)lerp(0, 9, 0, 7, a), secs(a < 7 ? a : 7)};
    m.agents[1] = {"code-reviewer", "review ui_session.cpp", a < 14 ? "running" : "done",
                   (uint32_t)lerp(6000, 48000, 0, 14, a), (int)lerp(0, 14, 0, 14, a), secs(a < 14 ? a : 14)};
    m.agents[2] = {"test-runner", "run the smoke test", a < 20 ? "running" : "done",
                   (uint32_t)lerp(3000, 22000, 0, 20, a), (int)lerp(0, 11, 0, 20, a), secs(a < 20 ? a : 20)};
    m.agentsRunning = 0;
    for (int k = 0; k < 3; k++) if (!strcmp(m.agents[k].status, "running")) m.agentsRunning++;
    // Running ones first, as the mod sends them.
    if (m.agentsRunning && strcmp(m.agents[0].status, "running")) {
      Agent tmp[3];
      int n = 0;
      for (int k = 0; k < 3; k++) if (!strcmp(m.agents[k].status, "running")) tmp[n++] = m.agents[k];
      for (int k = 0; k < 3; k++) if (strcmp(m.agents[k].status, "running")) tmp[n++] = m.agents[k];
      for (int k = 0; k < 3; k++) m.agents[k] = tmp[k];
    }
    m.steps = 8 + (int)(a / 3);
  }
  if (t >= 27 && t < 37) {
    m.state = "tool";
    m.tool = "Agent";
    m.detail = "3 subagents";
  } else if (t >= 37 && t < 45) {
    m.state = "thinking";
  } else if (t >= 45 && t < 53) {
    m.state = "waiting";
    m.tool = "Bash";
    m.detail = "git push origin main";
    m.asked = 3;
    m.pending = 1;
  } else if (t >= 53 && t < 59) {
    m.state = "tool";
    m.tool = "Bash";
    m.detail = "git push origin main";
    m.asked = 3;
    m.allowed = 25;
    addLog(m, "bash: git push");
  } else if (t >= 59) {
    m.state = "idle";
    m.tool = "Bash";
    m.detail = "git push origin main";
    m.allowed = 26;
    m.asked = 3;
    m.agentsTotal = 0; m.agentsRunning = 0; m.nAgents = 0;
    m.hasLast = true; m.lastMs = 55000; m.lastCost = 0.84f; m.lastEdits = 2; m.lastErrors = 0;
    m.steps = 0; m.edits = 0; m.elapsedMs = 0;
    addLog(m, "bash: git push");
  }
  return true;
}

// ---- session 2: website, ends with a needs-you and a done ---------------------------
bool session2(Mock& m, float t) {
  m.sid = "demo-s2"; m.project = "website"; m.model = "Sonnet 5"; m.effort = "medium"; m.mode = "acceptEdits";
  m.compactions = 0;
  m.ttftMs = 610;
  limits(m, t);
  if (t < 62 || t >= 105) return false;
  m.ctxPct = lerp(12, 24, 62, 96, t);
  m.cost = lerp(0.42f, 0.71f, 62, 96, t);
  m.allowed = 9; m.asked = 1;
  m.running = t < 94;
  m.elapsedMs = secs(t - 62);
  m.steps = (int)((t - 62) / 3);
  addLog(m, "read index.tsx");
  if (t < 70) {
    m.state = "thinking";
  } else if (t < 86) {
    m.state = "tool";
    m.tool = t < 78 ? "WebFetch" : "Edit";
    m.detail = t < 78 ? "nextjs.org/docs" : "src/pages/index.tsx";
    m.edits = t < 78 ? 0 : 1;
  } else if (t < 91) {
    m.state = "waiting";
    m.tool = "Bash";
    m.detail = "npm run deploy";
    m.asked = 2;
    m.pending = 1;
    m.edits = 1;
  } else if (t < 94) {
    m.state = "tool";
    m.tool = "Bash";
    m.detail = "npm run deploy";
    m.asked = 2; m.allowed = 10; m.edits = 1;
  } else {
    m.state = "idle";
    m.asked = 2; m.allowed = 10;
    m.steps = 0; m.elapsedMs = 0;
    m.hasLast = true; m.lastMs = 32000; m.lastCost = 0.29f; m.lastEdits = 1; m.lastErrors = 0;
    addLog(m, "bash: npm run deploy");
  }
  return true;
}

// ---- session 3: infra, compacts and hits an error ---------------------------------------
bool session3(Mock& m, float t) {
  m.sid = "demo-s3"; m.project = "infra"; m.model = "Opus 5.5"; m.effort = "max"; m.mode = "plan";
  m.ttftMs = 1320;
  limits(m, t);
  if (t < 64 || t >= 105) return false;
  m.cost = lerp(7.80f, 8.15f, 64, 98, t);
  m.allowed = 41; m.asked = 4; m.denied = 2;
  m.running = t < 97;
  m.elapsedMs = secs(t - 64 + 95);
  m.steps = 14 + (int)((t - 64) / 4);
  addLog(m, "bash: terraform init");
  if (t < 74) {
    m.state = "compacting";
    m.compactions = 2;
    m.ctxPct = lerp(93, 95, 64, 74, t);
  } else {
    m.compactions = 3;
    m.ctxPct = lerp(21, 27, 74, 98, t);
    if (t < 80) {
      m.state = "tool";
      m.tool = "Bash";
      m.detail = "terraform plan";
    } else if (t < 86) {
      m.state = "error";
      m.tool = "Bash";
      m.detail = "terraform plan";
      m.errors = 1;
      addLog(m, "bash: terraform plan");
    } else if (t < 97) {
      m.state = "thinking";
      m.errors = 1;
    } else {
      m.state = "idle";
      m.steps = 0; m.elapsedMs = 0;
      m.hasLast = true; m.lastMs = 128000; m.lastCost = 0.35f; m.lastEdits = 0; m.lastErrors = 1;
    }
  }
  return true;
}

void push(const Mock& m) {
  JsonDocument d;
  d["v"] = 1;
  d["sid"] = m.sid;
  d["project"] = m.project;
  d["state"] = m.state;
  d["tool"] = m.tool;
  d["detail"] = m.detail;
  d["model"] = m.model;
  d["effort"] = m.effort;
  d["mode"] = m.mode;
  JsonObject ctx = d["ctx"].to<JsonObject>();
  ctx["pct"] = m.ctxPct;
  ctx["tokens"] = (uint32_t)(m.ctxPct / 100.0f * m.window);
  ctx["window"] = m.window;
  ctx["compactions"] = m.compactions;
  d["cost"] = m.cost;
  JsonArray lim = d["limits"].to<JsonArray>();
  JsonObject l5 = lim.add<JsonObject>();
  l5["kind"] = "5h"; l5["pct"] = m.l5; l5["resetInMs"] = m.r5;
  JsonObject l7 = lim.add<JsonObject>();
  l7["kind"] = "7d"; l7["pct"] = m.l7; l7["resetInMs"] = m.r7;
  JsonObject turn = d["turn"].to<JsonObject>();
  turn["running"] = m.running;
  turn["elapsedMs"] = m.elapsedMs;
  turn["steps"] = m.steps;
  turn["edits"] = m.edits;
  turn["errors"] = m.errors;
  if (m.hasLast) {
    JsonObject last = d["last"].to<JsonObject>();
    last["durationMs"] = m.lastMs;
    last["costDelta"] = m.lastCost;
    last["edits"] = m.lastEdits;
    last["errors"] = m.lastErrors;
  } else {
    d["last"] = nullptr;
  }
  JsonObject ag = d["agents"].to<JsonObject>();
  ag["running"] = m.agentsRunning;
  ag["total"] = m.agentsTotal;
  JsonArray list = ag["list"].to<JsonArray>();
  for (int i = 0; i < m.nAgents; i++) {
    JsonObject a = list.add<JsonObject>();
    a["name"] = m.agents[i].name;
    a["task"] = m.agents[i].task;
    a["status"] = m.agents[i].status;
    a["ctx"] = m.agents[i].ctx;
    a["steps"] = m.agents[i].steps;
    a["elapsedMs"] = m.agents[i].elapsedMs;
  }
  JsonObject gate = d["gate"].to<JsonObject>();
  gate["allowed"] = m.allowed;
  gate["asked"] = m.asked;
  gate["denied"] = m.denied;
  gate["pending"] = m.pending;
  d["ttftMs"] = m.ttftMs;
  JsonArray log = d["log"].to<JsonArray>();
  for (int i = 0; i < m.nLog; i++) log.add(m.log[i]);

  static char buf[2048];
  size_t n = serializeJson(d, buf, sizeof(buf));
  const char* err = "";
  if (sessionsApplyState(buf, n, err) != 200) Serial.printf("[demo] push %s: %s\n", m.sid, err);
}

void end(const char* sid) {
  if (sessionFind(sid) < 0) return;
  char buf[48];
  int n = snprintf(buf, sizeof(buf), "{\"v\":1,\"sid\":\"%s\"}", sid);
  const char* err = "";
  sessionsApplyEnd(buf, n, err);
}

}  // namespace

void demoBegin() {
  epoch = millis();
  Serial.println("[demo] demo mode: scripted sessions, real pushes ignored");
}

void demoTick(uint32_t now) {
  if (now - lastPush < PUSH_MS) return;
  lastPush = now;
  const float t = ((now - epoch) % LOOP_MS) / 1000.0f;

  typedef bool (*Script)(Mock&, float);
  static const Script SCRIPTS[] = {session1, session2, session3};
  for (Script script : SCRIPTS) {
    Mock m;
    if (script(m, t)) push(m);
    else end(m.sid);
  }

  // Show the session and page that fit the moment instead of the timed rotation, so every
  // screen comes up on cue. Needs-you and done still take over by themselves.
  const char* sid = "demo-s1";
  ui::Page page = ui::Page::Session;
  if (t >= 27 && t < 37) page = ui::Page::Agents;
  else if (t >= 37 && t < 45) page = ui::Page::Limits;
  else if (t >= 62 && t < 68) sid = "demo-s2";
  else if (t >= 68 && t < 86) sid = "demo-s3";
  else if (t >= 86 && t < 92) sid = "demo-s2";
  const int slot = sessionFind(sid);
  if (slot >= 0) ui::pinPage((int8_t)slot, page, PUSH_MS * 3);
}

#endif  // ORB_DEMO
