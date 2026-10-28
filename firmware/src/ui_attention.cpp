// =============================================================================
//  Attention ("NEEDS YOU") and turn-complete celebration.
//  Both are designed to be noticed from across the room: big pulsing colour on
//  the rim, minimal text in the middle.
// =============================================================================
#include "ui_common.h"
#include <math.h>

using namespace gfx;

namespace ui {

void drawAttention(const Session& s, const Frame& f) {
  const uint32_t now = f.now;
  const float p = 0.5f + 0.5f * sinf(now * (6.2831853f / 1100.0f));   // ~0.9 Hz heartbeat

  // Rim: soft inner halo + bright pulsing ring + two counter-rotating comets.
  glowRing(CX, CY, 108, 26, pal::AMBER, (uint8_t)(35 + 55 * p));
  ring(CX, CY, 114, 7, pal::AMBER, (uint8_t)(120 + 135 * p));
  for (int k = 0; k < 2; k++) {
    float a = fmodf(now * 0.12f + k * 180.0f, 360.0f);
    arc(CX, CY, 102, 2.5f, a, a + 42, pal::AMBER, 200);
    float b = fmodf(-(float)now * 0.07f + k * 180.0f + 90.0f, 360.0f);
    if (b < 0) b += 360;
    arc(CX, CY, 96, 1.5f, b, b + 24, scale(pal::AMBER, 160), 160);
  }

  spark(CX, 60, 15, Glyph::Waiting, now, pal::AMBER);

  font(F_BOLD);
  text("NEEDS YOU", (int32_t)CX, 99, mix(pal::AMBER, pal::TEXT, (uint8_t)(60 * p)));

  char buf[64];
  font(F_BODY);
  fitText(s.project, 2 * chordHalf(124, 90), buf, sizeof(buf));
  text(buf, (int32_t)CX, 124, pal::TEXT);

  if (s.tool[0]) {
    font(F_BOLD);
    fitText(s.tool, 2 * chordHalf(150, 92), buf, sizeof(buf));
    text(buf, (int32_t)CX, 150, pal::ORANGE_HI);
  }
  if (s.detail[0]) {
    font(F_SMALL);
    int half = chordHalf(172, 92);
    marquee(s.detail, (int32_t)(CX - half), 172, 2 * half, pal::DIM, now);
  }

  char t[16];
  fmtClock(now - s.stateSinceMs, t, sizeof(t));
  if (s.gatePending > 1) snprintf(buf, sizeof(buf), "waiting %s \xC2\xB7 %u pending", t, s.gatePending);
  else snprintf(buf, sizeof(buf), "waiting %s", t);
  font(F_SMALL);
  text(buf, (int32_t)CX, 194, pal::FAINT);

  sessionDots(f.slot, now, 214);
}

void drawDone(const Session& s, const Frame& f, uint32_t startMs, uint32_t durMs) {
  const uint32_t now = f.now;
  const float u = (float)(now - startMs) / (float)durMs;   // 0..1

  // Rim flash: two quick bright pulses, then settles to a soft ring.
  float flash = 0;
  if (u < 0.5f) flash = fabsf(sinf(u * 2 * 3.14159f * 2));
  ring(CX, CY, 114, 8, pal::GREEN, (uint8_t)(90 + 165 * flash));
  glowRing(CX, CY, 108, 18, pal::GREEN, (uint8_t)(30 + 70 * flash));

  // Ripples expanding from the centre.
  for (int k = 0; k < 3; k++) {
    float uk = u * 1.6f - k * 0.18f;
    if (uk <= 0 || uk >= 1) continue;
    float r = 26 + uk * 90;
    ring(CX, CY, r, 2.5f * (1 - uk) + 0.8f, pal::GREEN, (uint8_t)(200 * (1 - uk)));
  }

  // Check mark, drawn on over the first 450 ms.
  glow(CX, 104, 40, pal::GREEN, 60);
  const float ax = CX - 22, ay = 104, bx = CX - 7, by = 119, cx2 = CX + 23, cy2 = 89;
  float d = (float)(now - startMs) / 450.0f;
  if (d > 1) d = 1;
  float l1 = 0.35f;   // first stroke share of the path
  if (d > 0) {
    float k = d < l1 ? d / l1 : 1;
    wedge(ax, ay, 3.8f, ax + (bx - ax) * k, ay + (by - ay) * k, 3.8f, pal::GREEN);
  }
  if (d > l1) {
    float k = (d - l1) / (1 - l1);
    wedge(bx, by, 3.8f, bx + (cx2 - bx) * k, by + (cy2 - by) * k, 3.8f, pal::GREEN);
  }

  char buf[64];
  font(F_BODY);
  fitText(s.project, 2 * chordHalf(56, 92), buf, sizeof(buf));
  text(buf, (int32_t)CX, 56, pal::DIM);

  font(F_BOLD);
  text("DONE", (int32_t)CX, 152, pal::GREEN);

  if (s.hasLast) {
    char dur[16], cost[16];
    fmtClock(s.lastDurationMs, dur, sizeof(dur));
    fmtCost(s.lastCostDelta, cost, sizeof(cost), true);
    if (isnan(s.lastCostDelta))
      snprintf(buf, sizeof(buf), "%s \xC2\xB7 %u edits", dur, s.lastEdits);
    else
      snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s \xC2\xB7 %u edits", dur, cost, s.lastEdits);
    font(F_SMALL);
    text(buf, (int32_t)CX, 177, pal::TEXT);
    if (s.lastErrors) {
      snprintf(buf, sizeof(buf), "%u error%s", s.lastErrors, s.lastErrors == 1 ? "" : "s");
      text(buf, (int32_t)CX, 195, pal::RED);
    }
  }
}

}  // namespace ui
