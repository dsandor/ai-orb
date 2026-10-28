// Shared UI helpers: the spark glyph, gauges, dots, text runs, formatting.
#include "ui_common.h"
#include <math.h>

using namespace gfx;

namespace ui {

Glyph glyphFor(SState s) {
  switch (s) {
    case SState::Thinking:   return Glyph::Thinking;
    case SState::Tool:       return Glyph::Tool;
    case SState::Waiting:    return Glyph::Waiting;
    case SState::Error:      return Glyph::Error;
    case SState::Compacting: return Glyph::Compacting;
    default:                 return Glyph::Idle;
  }
}

uint16_t stateColor(SState s) {
  switch (s) {
    case SState::Thinking:   return pal::ORANGE;
    case SState::Tool:       return pal::ORANGE;
    case SState::Waiting:    return pal::AMBER;
    case SState::Error:      return pal::RED;
    case SState::Compacting: return pal::PURPLE;
    default:                 return scale(pal::ORANGE, 150);
  }
}

const char* stateLabel(SState s) {
  switch (s) {
    case SState::Thinking:   return "thinking";
    case SState::Tool:       return "running";
    case SState::Waiting:    return "needs you";
    case SState::Error:      return "error";
    case SState::Compacting: return "compacting";
    default:                 return "idle";
  }
}

uint16_t ctxColor(float pct) {
  if (pct < 50) return pal::GREEN;
  if (pct < 75) return mix(pal::GREEN, pal::AMBER, (uint8_t)((pct - 50) * 255 / 25));
  if (pct < 90) return mix(pal::AMBER, pal::RED, (uint8_t)((pct - 75) * 255 / 15));
  return pal::RED;
}

uint16_t limitColor(float pct, uint16_t base) {
  // Short transition bands: a long base->amber blend passes through grey.
  if (pct < 72) return base;
  if (pct < 76) return mix(base, pal::AMBER, (uint8_t)((pct - 72) * 255 / 4));
  if (pct < 88) return pal::AMBER;
  if (pct < 92) return mix(pal::AMBER, pal::RED, (uint8_t)((pct - 88) * 255 / 4));
  return pal::RED;
}

// -----------------------------------------------------------------------------
//  Spark: ten tapered rays around a core, in the spirit of the Claude mark.
//  Each state gets its own motion so it reads from across the room:
//    thinking   - slow spin, a breathing wave travels around the rays
//    tool       - a "chase" pulse runs ray to ray + a satellite orbits
//    compacting - the whole star squeezes in and out, counter-spinning
//    waiting    - full star, steady heartbeat with a halo
//    error      - shakes, then settles
//    idle       - still, barely breathing
// -----------------------------------------------------------------------------
void spark(float cx, float cy, float r, Glyph g, uint32_t t, uint16_t color, uint8_t alpha) {
  static const float shape[10] = {1.00f, 0.74f, 0.93f, 0.68f, 0.98f, 0.71f, 0.90f, 0.64f, 0.96f, 0.76f};
  constexpr int N = 10;
  float len[N];
  float rot = 0, ox = 0;
  uint8_t halo = 0;
  const float ft = (float)t;

  switch (g) {
    case Glyph::Thinking:
      rot = fmodf(ft * 0.03f, 360.0f);
      for (int i = 0; i < N; i++)
        len[i] = shape[i] * (0.55f + 0.45f * (0.5f + 0.5f * sinf(ft * 0.0055f - i * 1.2566f)));
      halo = 55;
      break;
    case Glyph::Tool: {
      rot = fmodf(ft * 0.012f, 360.0f);
      float head = fmodf(ft / 75.0f, (float)N);
      for (int i = 0; i < N; i++) {
        float d = fabsf(i - head);
        if (d > N / 2) d = N - d;
        len[i] = shape[i] * (0.5f + 0.5f * expf(-d * d / 1.6f));
      }
      halo = 45;
      break;
    }
    case Glyph::Compacting: {
      rot = -fmodf(ft * 0.06f, 360.0f);
      float s = 0.42f + 0.58f * (0.5f + 0.5f * sinf(ft * 0.004f));
      for (int i = 0; i < N; i++) len[i] = shape[i] * s;
      halo = 40;
      break;
    }
    case Glyph::Waiting: {
      float p = 0.5f + 0.5f * sinf(ft * 0.0057f);
      for (int i = 0; i < N; i++) len[i] = shape[i] * (0.84f + 0.16f * p);
      halo = (uint8_t)(40 + 80 * p);
      break;
    }
    case Glyph::Error: {
      float env = 1.0f - (float)(t % 1600) / 450.0f;
      if (env < 0) env = 0;
      ox = 2.5f * sinf(ft * 0.07f) * env;
      for (int i = 0; i < N; i++) len[i] = shape[i] * 0.9f;
      halo = 40;
      break;
    }
    case Glyph::Idle:
    default: {
      float b = 0.93f + 0.07f * sinf(ft * 0.0016f);
      for (int i = 0; i < N; i++) len[i] = shape[i] * b;
      break;
    }
  }

  const float x0 = cx + ox;
  if (halo) glow(x0, cy, r * 2.0f, color, (uint8_t)((uint16_t)halo * alpha / 255));
  const float baseR = r * 0.13f, tipR = r * 0.045f;
  for (int i = 0; i < N; i++) {
    float tx, ty;
    polar(x0, cy, r * len[i], rot + i * (360.0f / N), tx, ty);
    wedge(x0, cy, baseR, tx, ty, tipR, color, alpha);
  }
  disc(x0, cy, r * 0.17f, color, alpha);

  if (g == Glyph::Tool) {
    float a = fmodf(ft * 0.25f, 360.0f);
    float px, py;
    ring(cx, cy, r * 1.55f, 1.0f, color, (uint8_t)(35 * alpha / 255));
    polar(cx, cy, r * 1.55f, a, px, py);
    glow(px, py, r * 0.45f, color, (uint8_t)(120 * alpha / 255));
    disc(px, py, r * 0.11f, pal::ORANGE_HI, alpha);
  }
}

void gauge(float r, float w, float pct, uint16_t fill, uint16_t track, bool endGlow) {
  arc(CX, CY, r, w, G_START, G_END, track);
  if (pct < 0) return;
  if (pct > 100) pct = 100;
  float a1 = G_START + G_SWEEP * pct / 100.0f;
  if (endGlow) {
    float x, y;
    polar(CX, CY, r, a1, x, y);
    glow(x, y, w * 2.2f, fill, 110);
  }
  arc(CX, CY, r, w, G_START, a1, fill);
}

void gaugeShimmer(float r, float w, float pct, uint32_t t, uint16_t color) {
  float span = G_SWEEP * (pct > 100 ? 100 : pct) / 100.0f;
  if (span < 24) return;
  const uint32_t period = 2600;
  float u = (float)(t % period) / period;
  float head = G_START + 6 + (span - 6) * u;
  float tail = head - 22;
  if (tail < G_START) tail = G_START;
  uint8_t a = (uint8_t)(140 * sinf(3.14159f * u));
  arc(CX, CY, r, w * 0.55f, tail, head, mix(color, pal::TEXT, 150), a);
}

void sessionDots(int8_t focusSlot, uint32_t now, float y) {
  uint8_t n = sessionsCount();
  if (n < 2) return;
  const float gap = 11.0f;
  float x = CX - (n - 1) * gap / 2.0f;
  for (uint8_t i = 0; i < ORB_MAX_SESSIONS; i++) {
    Session* s = sessionAt(i);
    if (!s) continue;
    bool focus = (int8_t)i == focusSlot;
    uint16_t c = pal::DIM;
    uint8_t a = 255;
    if (s->state == SState::Waiting) {
      c = pal::AMBER;
      a = (uint8_t)(150 + 105 * (0.5f + 0.5f * sinf(now * 0.0057f)));
    } else if (sessionIsStale(*s, now)) {
      c = pal::FAINT;
    } else if (sessionIsBusy(*s, now)) {
      c = pal::ORANGE_HI;
      a = 170;
    }
    if (focus) {
      glow(x, y, 7, pal::ORANGE, 90);
      disc(x, y, 3.3f, pal::ORANGE);
    } else {
      disc(x, y, 2.4f, c, a);
    }
    x += gap;
  }
}

void textRuns(const char* const* parts, const uint16_t* colors, int n, float cx, int32_t y,
              gfx::Font f, int maxW) {
  font(f);
  int widths[8];
  if (n > 8) n = 8;
  int total = 0;
  for (int i = 0; i < n; i++) { widths[i] = textWidth(parts[i]); total += widths[i]; }
  while (n > 1 && total > maxW) { n--; total -= widths[n]; }
  if (n == 1 && total > maxW) {
    char buf[64];
    fitText(parts[0], maxW, buf, sizeof(buf));
    text(buf, (int32_t)cx, y, colors[0]);
    return;
  }
  int32_t x = (int32_t)(cx - total / 2.0f);
  for (int i = 0; i < n; i++) {
    text(parts[i], x, y, colors[i], lgfx::middle_left);
    x += widths[i];
  }
}

int chordHalf(float y, float r) {
  float dy = fabsf(y - CY);
  if (dy >= r) return 0;
  return (int)sqrtf(r * r - dy * dy);
}

float approach(float cur, float target, float k) {
  if (isnan(cur)) return target;
  float d = target - cur;
  if (fabsf(d) < 0.05f) return target;
  return cur + d * k;
}

void fmtClock(uint32_t ms, char* out, size_t n) {
  uint32_t s = ms / 1000;
  uint32_t h = s / 3600, m = (s / 60) % 60, sec = s % 60;
  if (h) snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)sec);
  else   snprintf(out, n, "%lu:%02lu", (unsigned long)m, (unsigned long)sec);
}

void fmtCountdown(int64_t ms, char* out, size_t n) {
  if (ms < 0) { snprintf(out, n, "--"); return; }
  uint32_t s = (uint32_t)(ms / 1000);
  uint32_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60;
  if (d)      snprintf(out, n, "%lud %luh", (unsigned long)d, (unsigned long)h);
  else if (h) snprintf(out, n, "%luh %02lum", (unsigned long)h, (unsigned long)m);
  else if (m) snprintf(out, n, "%lum", (unsigned long)m);
  else        snprintf(out, n, "<1m");
}

void fmtTokens(uint32_t t, char* out, size_t n) {
  if (t < 1000)          snprintf(out, n, "%lu", (unsigned long)t);
  else if (t < 10000)    snprintf(out, n, "%.1fk", t / 1000.0f);
  else if (t < 1000000)  snprintf(out, n, "%luk", (unsigned long)(t / 1000));
  else                   snprintf(out, n, "%.1fM", t / 1000000.0f);
}

void fmtCost(float c, char* out, size_t n, bool sign) {
  if (isnan(c)) { snprintf(out, n, "$--"); return; }
  const char* pre = sign ? (c < 0 ? "-" : "+") : "";
  float a = fabsf(c);
  if (a < 1000) snprintf(out, n, "%s$%.2f", pre, a);
  else          snprintf(out, n, "%s$%.0f", pre, a);
}

void staleOverlay(const Session& s, uint32_t now) {
  fade(95);
  char ago[16], buf[40];
  uint32_t age = sessionAge(s, now);
  if (age < 60000) snprintf(ago, sizeof(ago), "%lus", (unsigned long)(age / 1000));
  else fmtCountdown((int64_t)age, ago, sizeof(ago));
  snprintf(buf, sizeof(buf), "no signal \xC2\xB7 %s", ago);
  font(F_SMALL);
  int w = textWidth(buf) + 20;
  canvas().fillRoundRect((int32_t)(CX - w / 2), 31, w, 19, 9, pal::BG);
  canvas().drawRoundRect((int32_t)(CX - w / 2), 31, w, 19, 9, scale(pal::AMBER, 90));
  text(buf, (int32_t)CX, 40, pal::AMBER);
}

}  // namespace ui
