#pragma once
// =============================================================================
//  Session table: parsing of /api/state + /api/end, staleness, focus rules.
//  All strings are fixed-size, UTF-8-safe truncated copies; numbers are
//  range-checked. Nothing in here allocates after boot.
// =============================================================================
#include <Arduino.h>
#include "config.h"

enum class SState : uint8_t { Idle, Thinking, Tool, Waiting, Error, Compacting };
enum class AStatus : uint8_t { Running, Done, Error };

struct AgentInfo {
  char     name[20];
  char     task[36];
  AStatus  status;
  uint32_t ctx;
  uint16_t steps;
  uint32_t elapsedMs;
};

struct LimitInfo {
  bool     present;
  float    pct;           // 0..100+
  int64_t  resetInMs;     // -1 = unknown
};

struct Session {
  bool     used;
  char     sid[24];
  char     project[28];
  SState   state;
  char     tool[28];
  char     detail[44];
  char     model[28];
  char     effort[12];
  char     mode[20];

  float    ctxPct;        // NAN = unknown
  uint32_t ctxTokens;
  uint32_t ctxWindow;
  uint16_t compactions;
  float    cost;          // NAN = unknown

  LimitInfo lim5h;
  LimitInfo lim7d;

  bool     turnRunning;
  uint32_t turnElapsedMs;
  uint16_t turnSteps, turnEdits, turnErrors;

  bool     hasLast;
  uint32_t lastDurationMs;
  float    lastCostDelta;  // NAN = unknown
  uint16_t lastEdits, lastErrors;

  uint8_t  agentsRunning, agentsTotal, agentCount;
  AgentInfo agents[ORB_MAX_AGENTS];

  uint16_t gateAllowed, gateAsked, gateDenied, gatePending;
  int32_t  ttftMs;         // -1 = unknown

  uint8_t  logCount;
  char     log[ORB_MAX_LOG][36];

  // --- device-side bookkeeping (millis()) ---
  uint32_t rxMs;           // last packet received
  uint32_t changedMs;      // last meaningful change (state/tool/detail/steps)
  uint32_t stateSinceMs;   // when `state` last changed value
  uint32_t doneAtMs;       // last running -> idle transition (0 = never)
};

// Event flags the UI consumes (set by sessionsApplyState, cleared by UI).
struct SessionEvents {
  int8_t turnDoneIdx = -1;   // session that just finished a turn
};

void        sessionsBegin();
// Parses a JSON body. Returns HTTP status (200/400) and fills `err` on 400.
int         sessionsApplyState(const char* body, size_t len, const char*& err);
int         sessionsApplyEnd(const char* body, size_t len, const char*& err);
void        sessionsTick(uint32_t now);    // staleness / drop
uint8_t     sessionsCount();
Session*    sessionAt(uint8_t idx);        // nullptr if slot unused
int         sessionFind(const char* sid);
uint32_t    sessionAge(const Session& s, uint32_t now);      // ms since its last push, never negative
bool        sessionIsStale(const Session& s, uint32_t now);
bool        sessionIsBusy(const Session& s, uint32_t now);   // fresh + non-idle
SessionEvents& sessionEvents();

// Focus rules (protocol.md "Device behaviour").
//   attention: index of a fresh session in `waiting` (rotates every 10 s
//              between several), or -1.
//   focus:     most recently changed busy session, rotating every 10 s when
//              several are busy; else the most recently changed session; -1
//              when the table is empty.
int         sessionsAttention(uint32_t now);
int         sessionsFocus(uint32_t now, bool* multiBusy = nullptr);
uint8_t     sessionsBusyCount(uint32_t now);
uint32_t    sessionsLastChangeMs();

// Live values advanced locally from the receipt time.
uint32_t    sessionTurnElapsed(const Session& s, uint32_t now);
int64_t     sessionResetIn(const Session& s, const LimitInfo& l, uint32_t now);
uint32_t    sessionAgentElapsed(const Session& s, const AgentInfo& a, uint32_t now);

const char* sstateName(SState s);
