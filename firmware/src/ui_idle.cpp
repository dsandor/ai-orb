// =============================================================================
//  Idle clock, boot / setup / connecting / connected screens, info overlay.
// =============================================================================
#include "ui_common.h"
#include "net.h"
#include "settings.h"
#include "web.h"
#include "config.h"
#include <math.h>
#include <time.h>

using namespace gfx;

namespace ui {

// ---- Idle clock ----------------------------------------------------------------------
void drawIdle(const Frame& f) {
  const uint32_t now = f.now;
  time_t tnow = time(nullptr);
  struct tm tmv;
  localtime_r(&tnow, &tmv);
  const bool valid = netTimeValid();

  // Hour pips
  for (int i = 0; i < 12; i++) {
    float x, y;
    polar(CX, CY, 106, i * 30.0f, x, y);
    disc(x, y, (i % 3 == 0) ? 1.9f : 1.2f, (i % 3 == 0) ? pal::DIM : pal::FAINT);
  }
  ring(CX, CY, 115, 1.0f, pal::TRACK_HI);

  // Seconds comet: smooth sweep using millis() for the sub-second part.
  if (valid) {
    static int lastSec = -1;
    static uint32_t secEdge = 0;
    if (tmv.tm_sec != lastSec) { lastSec = tmv.tm_sec; secEdge = now; }
    float frac = (float)(now - secEdge) / 1000.0f;
    if (frac > 1) frac = 1;
    float a = (tmv.tm_sec + frac) * 6.0f;
    for (int k = 0; k < 6; k++) {   // fading tail
      float a1 = a - k * 7.0f, a0 = a1 - 7.0f;
      arc(CX, CY, 115, 3.0f, a0, a1, pal::ORANGE, (uint8_t)(200 - k * 33), false);
    }
    float x, y;
    polar(CX, CY, 115, a, x, y);
    glow(x, y, 9, pal::ORANGE, 150);
    disc(x, y, 2.6f, pal::ORANGE_HI);
  }

  spark(CX, 54, 11, Glyph::Idle, now, scale(pal::ORANGE, 200));

  char buf[48];
  font(F_CLOCK);
  if (valid) {
    int h = tmv.tm_hour;
    if (!settings.clock24h) { h %= 12; if (h == 0) h = 12; }
    snprintf(buf, sizeof(buf), settings.clock24h ? "%02d:%02d" : "%d:%02d", h, tmv.tm_min);
  } else {
    strlcpy(buf, "--:--", sizeof(buf));
  }
  text(buf, (int32_t)CX, 106, pal::TEXT);

  font(F_BODY);
  if (valid) {
    static const char* const DAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const MON[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (!settings.clock24h)
      snprintf(buf, sizeof(buf), "%s %d %s \xC2\xB7 %s", DAYS[tmv.tm_wday % 7], tmv.tm_mday,
               MON[tmv.tm_mon % 12], tmv.tm_hour < 12 ? "am" : "pm");
    else
      snprintf(buf, sizeof(buf), "%s %d %s", DAYS[tmv.tm_wday % 7], tmv.tm_mday, MON[tmv.tm_mon % 12]);
    text(buf, (int32_t)CX, 146, pal::DIM);
  } else {
    text(netLinkUp() ? "syncing time" : "offline", (int32_t)CX, 146, pal::FAINT);
  }

  font(F_SMALL);
  uint8_t n = sessionsCount();
  if (n == 0) {
    text("no active sessions", (int32_t)CX, 170, pal::FAINT);
  } else {
    // Sessions exist but have all been idle for a while.
    int fi = sessionsFocus(now);
    Session* s = fi >= 0 ? sessionAt(fi) : nullptr;
    if (n == 1 && s) snprintf(buf, sizeof(buf), "%s \xC2\xB7 idle", s->project);
    else snprintf(buf, sizeof(buf), "%u sessions idle", n);
    char fit[48];
    fitText(buf, 2 * chordHalf(170, 100), fit, sizeof(fit));
    text(fit, (int32_t)CX, 170, pal::DIM);
  }

  if (netLinkUp()) {
    snprintf(buf, sizeof(buf), "%s.local", netHostname());
    text(buf, (int32_t)CX, 192, scale(pal::DIM, 200));
    text(netIp(), (int32_t)CX, 208, pal::FAINT);
  } else {
    text("wi-fi offline", (int32_t)CX, 196, pal::RED);
  }
}

// ---- Setup (AP portal) -----------------------------------------------------------------
void drawSetup(const Frame& f) {
  const uint32_t now = f.now;
  // Slowly rotating dashed rim
  float base = fmodf(now * 0.02f, 360.0f);
  for (int i = 0; i < 24; i++) {
    float a = base + i * 15.0f;
    arc(CX, CY, 113, 3, a, a + 7, i % 6 == 0 ? pal::ORANGE : pal::TRACK_HI);
  }
  spark(CX, 56, 14, Glyph::Thinking, now, pal::ORANGE);

  font(F_BOLD);
  text("AI Orb", (int32_t)CX, 94, pal::TEXT);
  font(F_SMALL);
  text("Wi-Fi setup", (int32_t)CX, 114, pal::DIM);
  text("join network", (int32_t)CX, 138, pal::FAINT);
  font(F_BODY);
  text(ORB_AP_SSID, (int32_t)CX, 156, pal::ORANGE_HI);
  font(F_SMALL);
  char buf[40];
  snprintf(buf, sizeof(buf), "then open %s", netIp());
  text(buf, (int32_t)CX, 178, pal::DIM);
  if (settingsHasWifi()) {
    char fit[40];
    snprintf(buf, sizeof(buf), "retrying %s", netSsid());
    fitText(buf, 130, fit, sizeof(fit));
    text(fit, (int32_t)CX, 198, pal::FAINT);
  }
}

// ---- Connecting ------------------------------------------------------------------------
void drawConnecting(const Frame& f) {
  const uint32_t now = f.now;
  ring(CX, CY, 113, 5, pal::TRACK);
  float a = fmodf(now * 0.27f, 360.0f);
  float len = 60 + 50 * sinf(now * 0.003f);
  arc(CX, CY, 113, 5, a, a + len, pal::ORANGE);
  float x, y;
  polar(CX, CY, 113, a + len, x, y);
  glow(x, y, 12, pal::ORANGE, 140);

  spark(CX, 108, 22, Glyph::Thinking, now, pal::ORANGE);
  font(F_BODY);
  text("connecting", (int32_t)CX, 160, pal::TEXT);
  font(F_SMALL);
  char fit[40];
  fitText(netSsid(), 140, fit, sizeof(fit));
  text(fit, (int32_t)CX, 180, pal::DIM);
  text("fw " FW_VERSION, (int32_t)CX, 204, pal::FAINT);
}

// ---- Connected splash --------------------------------------------------------------------
void drawConnected(const Frame& f, uint32_t sinceMs) {
  const uint32_t now = f.now;
  float u = (float)(now - sinceMs) / 700.0f;
  if (u > 1) u = 1;
  float e = 1 - (1 - u) * (1 - u) * (1 - u);   // ease-out
  ring(CX, CY, 113, 5, pal::TRACK);
  arc(CX, CY, 113, 5, 0, 360 * e, pal::GREEN, 255, e < 1);
  if (e >= 1) glowRing(CX, CY, 110, 10, pal::GREEN, 50);

  spark(CX, 66, 15, Glyph::Idle, now, pal::ORANGE);
  font(F_BODY);
  text("connected", (int32_t)CX, 104, pal::GREEN);
  font(F_BOLD);
  text(netIp(), (int32_t)CX, 134, pal::TEXT);
  font(F_SMALL);
  char buf[48];
  snprintf(buf, sizeof(buf), "%s.local", netHostname());
  text(buf, (int32_t)CX, 160, pal::DIM);
  text(settings.token[0] ? "token required" : "no token set", (int32_t)CX, 182, pal::FAINT);
}

// ---- Info overlay (long press) --------------------------------------------------------------
void drawInfo(const Frame& f) {
  ring(CX, CY, 114, 2, pal::ORANGE, 160);
  spark(CX, 34, 9, Glyph::Idle, f.now, pal::ORANGE);
  char buf[64];
  struct Row { const char* k; String v; };
  uint32_t up = f.now / 1000;
  snprintf(buf, sizeof(buf), "%luh %02lum", (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60));
  Row rows[] = {
    {"host", String(netHostname()) + ".local"},
    {"ip", netIp()},
    {"wi-fi", netLinkUp() ? String(netRssi()) + " dBm" : String("offline")},
    {"fw", FW_VERSION},
    {"sessions", String(sessionsCount())},
    {"requests", String(webRequestCount())},
    {"uptime", buf},
    {"heap", String(ESP.getFreeHeap() / 1024) + " KB"},
    {"canvas", canvasInfo()},
  };
  font(F_SMALL);
  int y = 58;
  for (const Row& r : rows) {
    int half = chordHalf(y, 104);
    if (half > 84) half = 84;
    text(r.k, (int32_t)(CX - half + 6), y, pal::DIM, lgfx::middle_left);
    char fit[40];
    fitText(r.v.c_str(), half + 30, fit, sizeof(fit));
    text(fit, (int32_t)(CX + half - 6), y, pal::TEXT, lgfx::middle_right);
    y += 17;
  }
}

}  // namespace ui
