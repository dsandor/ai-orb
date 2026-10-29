// =============================================================================
//  Screen manager
//  Priority (highest first):
//    info overlay (long press) > setup AP > first connect > connected splash >
//    attention (any fresh `waiting` session) > turn-complete flash >
//    focused session pages > idle clock
//  Focused session pages (Session, Agents if any, Limits) auto-rotate every
//  settings.rotateSec. When several sessions are busy, the focus rotates per
//  protocol (10 s) and only the main Session page is shown for each.
//  View changes cross-fade through black (110 ms out, 170 ms in).
// =============================================================================
#include "ui.h"
#include "ui_common.h"
#include "net.h"
#include "settings.h"
#include "config.h"

using namespace gfx;

namespace ui {

namespace {

enum class View : uint8_t { None, Setup, Connecting, Connected, Idle, Session, Agents, Limits,
                            Attention, Done, Info };

struct Key {
  View   v = View::None;
  int8_t slot = -1;
  bool operator!=(const Key& o) const { return v != o.v || slot != o.slot; }
};

constexpr uint32_t FADE_OUT_MS = 110;
constexpr uint32_t FADE_IN_MS  = 170;
constexpr uint32_t CONNECTED_SPLASH_MS = 3500;

Key      shown, pending;
enum class Phase : uint8_t { None, Out, In } phase = Phase::None;
uint32_t phaseStart = 0;

uint32_t lastFrame = 0;
int8_t   doneSlot = -1;
uint32_t doneStart = 0;
bool     everConnected = false;
uint32_t connectedAt = 0;
uint32_t infoUntil = 0;
uint32_t manualUntil = 0;
int8_t   manualSlot = -1;
uint8_t  manualPage = 0;
int8_t   lastFocus = -1;
uint32_t pageEpoch = 0;
int8_t   curSlot = -1;
uint8_t  curPage = 0, curPages = 1;
bool     curMulti = false;

// Frame timing stats (serial log every 10 s)
uint32_t statFrames = 0, statBusyUs = 0, statSince = 0;

uint8_t buildPages(const Session& s, View* out) {
  uint8_t n = 0;
  out[n++] = View::Session;
  if (s.agentCount > 0 || s.agentsTotal > 0) out[n++] = View::Agents;
  out[n++] = View::Limits;
  return n;
}

bool manualActive(uint32_t now) {
  return manualUntil && (int32_t)(manualUntil - now) > 0 && manualSlot >= 0 && sessionAt(manualSlot);
}

Key computeTarget(uint32_t now) {
  Key k;
  curMulti = false;
  if (infoUntil && (int32_t)(infoUntil - now) > 0) { k.v = View::Info; return k; }
  infoUntil = 0;

  NetState ns = netState();
  if (ns == NetState::Connected && !everConnected) {
    everConnected = true;
    connectedAt = now ? now : 1;
  }
  if (ns == NetState::Setup) { k.v = View::Setup; return k; }
  if (ns == NetState::Connecting && !everConnected) { k.v = View::Connecting; return k; }
  if (connectedAt && now - connectedAt < CONNECTED_SPLASH_MS && sessionsCount() == 0) {
    k.v = View::Connected;
    return k;
  }

  SessionEvents& ev = sessionEvents();
  if (ev.turnDoneIdx >= 0) {
    Session* s = sessionAt(ev.turnDoneIdx);
    if (s && !sessionIsStale(*s, now)) {
      doneSlot = ev.turnDoneIdx;
      doneStart = now;
    }
    ev.turnDoneIdx = -1;
  }

  int att = sessionsAttention(now);
  if (att >= 0) { k.v = View::Attention; k.slot = att; curSlot = att; return k; }

  if (doneSlot >= 0) {
    if (now - doneStart < ORB_DONE_FLASH_MS && sessionAt(doneSlot)) {
      k.v = View::Done;
      k.slot = doneSlot;
      return k;
    }
    doneSlot = -1;
  }

  bool multi = false;
  int fs = sessionsFocus(now, &multi);
  const bool manual = manualActive(now);
  if (manual) { fs = manualSlot; multi = false; }
  if (fs < 0) { k.v = View::Idle; curSlot = -1; return k; }
  if (!manual && sessionsBusyCount(now) == 0 && now - sessionsLastChangeMs() > ORB_IDLE_CLOCK_MS) {
    k.v = View::Idle;
    curSlot = -1;
    return k;
  }

  Session* s = sessionAt(fs);
  View pages[3];
  uint8_t n = buildPages(*s, pages);
  if (fs != lastFocus) { lastFocus = fs; pageEpoch = now; }
  uint8_t page;
  if (manual)      page = manualPage % n;
  else if (multi)  page = 0;
  else             page = ((now - pageEpoch) / (settings.rotateSec * 1000UL)) % n;

  curSlot = fs; curPage = page; curPages = n; curMulti = multi;
  k.v = pages[page];
  k.slot = fs;
  return k;
}

bool viewIsBusy(const Key& k, uint32_t now) {
  switch (k.v) {
    case View::Idle:
    case View::Info:
      return false;
    case View::Session:
    case View::Agents:
    case View::Limits: {
      Session* s = sessionAt(k.slot);
      return s && sessionIsBusy(*s, now);
    }
    default:
      return true;
  }
}

void render(const Key& k, uint32_t now) {
  clear(pal::BG);
  Frame f{now, k.slot, curPage, curPages, curMulti};
  Session* s = k.slot >= 0 ? sessionAt(k.slot) : nullptr;
  switch (k.v) {
    case View::Setup:      drawSetup(f); break;
    case View::Connecting: drawConnecting(f); break;
    case View::Connected:  drawConnected(f, connectedAt); break;
    case View::Idle:       drawIdle(f); break;
    case View::Info:       drawInfo(f); break;
    case View::Session:    if (s) drawSession(*s, f); break;
    case View::Agents:     if (s) drawAgents(*s, f); break;
    case View::Limits:     if (s) drawLimits(*s, f); break;
    case View::Attention:  if (s) drawAttention(*s, f); break;
    case View::Done:       if (s) drawDone(*s, f, doneStart, ORB_DONE_FLASH_MS); break;
    case View::None:       break;
  }
}

}  // namespace

void begin() {
  statSince = millis();
}

bool tick(uint32_t now) {
  Key target = computeTarget(now);

  // Transition state machine.
  if (phase == Phase::None && target != shown) {
    if (shown.v == View::None) {
      shown = target;
      phase = Phase::In;
    } else {
      pending = target;
      phase = Phase::Out;
    }
    phaseStart = now;
  } else if (phase == Phase::Out) {
    pending = target;   // follow the latest target while fading out
    if (now - phaseStart >= FADE_OUT_MS) {
      shown = pending;
      phase = Phase::In;
      phaseStart = now;
    }
  } else if (phase == Phase::In && now - phaseStart >= FADE_IN_MS) {
    phase = Phase::None;
  }

  const bool busy = phase != Phase::None || viewIsBusy(shown, now);
  const uint32_t interval = 1000 / (busy ? ORB_FPS_ACTIVE : ORB_FPS_IDLE);
  if (now - lastFrame < interval) return false;
  lastFrame = now;

  uint32_t t0 = micros();
  render(shown, now);
  if (phase == Phase::Out) {
    uint32_t e = now - phaseStart;
    fade((uint8_t)(255 - (e >= FADE_OUT_MS ? 255 : e * 255 / FADE_OUT_MS)));
  } else if (phase == Phase::In) {
    uint32_t e = now - phaseStart;
    uint32_t lv = e >= FADE_IN_MS ? 255 : e * 255 / FADE_IN_MS;
    fade((uint8_t)(lv * lv / 255));   // ease-in
  }
  present();
  statBusyUs += micros() - t0;
  statFrames++;
  if (now - statSince >= 10000) {
    Serial.printf("[ui] %lu fps, %lu us/frame render+push, heap %lu\n",
                  (unsigned long)(statFrames * 1000 / (now - statSince)),
                  (unsigned long)(statFrames ? statBusyUs / statFrames : 0),
                  (unsigned long)ESP.getFreeHeap());
    statFrames = 0;
    statBusyUs = 0;
    statSince = now;
  }
  return true;
}

void onShortPress() {
  const uint32_t now = millis();
  if (infoUntil) { infoUntil = 0; return; }
  doneSlot = -1;
  if (sessionsCount() == 0) return;
  if (!manualActive(now)) {
    manualSlot = curSlot >= 0 ? curSlot : (int8_t)sessionsFocus(now);
    manualPage = curMulti ? 0 : curPage;
  }
  Session* s = sessionAt(manualSlot);
  if (!s) return;
  View pages[3];
  uint8_t n = buildPages(*s, pages);
  manualPage++;
  if (manualPage >= n) {
    manualPage = 0;
    for (int k = 1; k <= ORB_MAX_SESSIONS; k++) {
      int j = (manualSlot + k) % ORB_MAX_SESSIONS;
      if (sessionAt(j)) { manualSlot = (int8_t)j; break; }
    }
  }
  manualUntil = now + ORB_MANUAL_HOLD_MS;
}

void pinPage(int8_t slot, Page page, uint32_t holdMs) {
  Session* s = sessionAt(slot);
  if (!s) return;
  const View want = page == Page::Agents ? View::Agents : page == Page::Limits ? View::Limits : View::Session;
  View pages[3];
  uint8_t n = buildPages(*s, pages);
  for (uint8_t i = 0; i < n; i++) {
    if (pages[i] != want) continue;
    manualSlot = slot;
    manualPage = i;
    manualUntil = millis() + holdMs;
    return;
  }
}

void onLongPress() {
  infoUntil = infoUntil ? 0 : millis() + 10000;
}

}  // namespace ui
