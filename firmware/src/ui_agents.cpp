// =============================================================================
//  Agents screen: up to 4 subagent lanes.
//    outer ring : one segment per agent (running = pulsing orange,
//                 done = green, error = red, not listed = faint)
//    lanes      : status dot, name, elapsed / task, steps, and an activity
//                 rail (sliding comet while running, solid when finished)
// =============================================================================
#include "ui_common.h"
#include <math.h>

using namespace gfx;

namespace ui {

static uint16_t agentColor(AStatus st) {
  switch (st) {
    case AStatus::Done:  return pal::GREEN;
    case AStatus::Error: return pal::RED;
    default:             return pal::ORANGE;
  }
}

void drawAgents(const Session& s, const Frame& f) {
  const uint32_t now = f.now;
  const bool stale = sessionIsStale(s, now);

  // ---- segmented outer ring ------------------------------------------------------
  uint8_t total = s.agentsTotal ? s.agentsTotal : s.agentCount;
  if (total > 12) total = 12;
  if (total == 0) total = 1;
  const float gapDeg = total > 1 ? 5.0f : 0.0f;
  const float seg = (G_SWEEP - gapDeg * (total - 1)) / total;
  for (uint8_t i = 0; i < total; i++) {
    float a0 = G_START + i * (seg + gapDeg);
    float a1 = a0 + seg;
    if (i < s.agentCount) {
      const AgentInfo& ag = s.agents[i];
      uint16_t c = agentColor(ag.status);
      if (ag.status == AStatus::Running && !stale) {
        float p = 0.5f + 0.5f * sinf(now * 0.005f - i * 1.1f);
        arc(CX, CY, 113, 7, a0, a1, scale(c, 70));
        // sweeping highlight inside the segment
        float u = (float)((now + i * 400) % 1800) / 1800.0f;
        float h = a0 + (a1 - a0) * u;
        float t0 = h - 12 < a0 ? a0 : h - 12;
        arc(CX, CY, 113, 7, t0, h, c, (uint8_t)(150 + 105 * p));
      } else {
        arc(CX, CY, 113, 7, a0, a1, c, ag.status == AStatus::Done ? 200 : 255);
      }
    } else {
      // Counted in `total` but not in the list (list is capped at 4).
      bool running = i < s.agentsRunning;
      arc(CX, CY, 113, 7, a0, a1, running ? scale(pal::ORANGE, 120) : pal::TRACK_HI);
    }
  }

  // ---- title ---------------------------------------------------------------------
  char a[24], b[32];
  snprintf(a, sizeof(a), "%u", s.agentsRunning);
  snprintf(b, sizeof(b), "/%u agents", s.agentsTotal ? s.agentsTotal : s.agentCount);
  {
    const char* parts[] = {a, b};
    const uint16_t cols[] = {s.agentsRunning ? pal::ORANGE_HI : pal::DIM, pal::DIM};
    textRuns(parts, cols, 2, CX, 40, F_SMALL, 2 * chordHalf(40, 102));
  }

  // ---- lanes ---------------------------------------------------------------------
  const uint8_t n = s.agentCount;
  if (n == 0) {
    font(F_BODY);
    text("no agents", (int32_t)CX, 120, pal::FAINT);
  }
  const float laneH = 38.0f;
  const float top = 124.0f - n * laneH / 2.0f;   // vertically centred block
  for (uint8_t i = 0; i < n; i++) {
    const AgentInfo& ag = s.agents[i];
    const float yc = top + laneH * i + laneH / 2.0f;
    // usable width: chord at the lane edge furthest from centre
    float edge = fabsf(yc - CY) + 14;
    int half = chordHalf(CY + edge, 102) - 4;
    if (half < 40) half = 40;
    const int32_t xl = (int32_t)(CX - half), xr = (int32_t)(CX + half);
    const int32_t y1 = (int32_t)(yc - 8), y2 = (int32_t)(yc + 8);
    const uint16_t c = agentColor(ag.status);

    // status dot (+ spinner ring while running)
    const float dx = xl + 6, dy = y1;
    if (ag.status == AStatus::Running && !stale) {
      float ang = fmodf(now * 0.4f + i * 90, 360.0f);
      arc(dx, dy, 6.5f, 1.6f, ang, ang + 220, c, 200);
      disc(dx, dy, 2.8f, c);
    } else {
      disc(dx, dy, 3.6f, c);
    }

    char el[16], buf[48];
    fmtClock(sessionAgentElapsed(s, ag, now), el, sizeof(el));
    font(F_SMALL);
    int elw = textWidth(el);
    text(el, xr, y1, ag.status == AStatus::Running ? pal::TEXT : pal::DIM, lgfx::middle_right);

    font(F_BODY);
    fitText(ag.name, (xr - elw - 8) - (xl + 16), buf, sizeof(buf));
    text(buf, xl + 16, y1, ag.status == AStatus::Error ? pal::RED : pal::TEXT, lgfx::middle_left);

    font(F_SMALL);
    char st[16];
    snprintf(st, sizeof(st), "%u st", ag.steps);
    int stw = textWidth(st);
    text(st, xr, y2, pal::FAINT, lgfx::middle_right);
    if (ag.task[0]) {
      int w = (xr - stw - 8) - (xl + 16);
      if (ag.status == AStatus::Running) marquee(ag.task, xl + 16, y2, w, pal::DIM, now + i * 700);
      else {
        fitText(ag.task, w, buf, sizeof(buf));
        text(buf, xl + 16, y2, pal::FAINT, lgfx::middle_left);
      }
    }

    // activity rail
    const float ry = yc + 18;
    const float rx0 = xl + 16, rx1 = (float)xr;
    if (ag.status == AStatus::Running && !stale) {
      wedge(rx0, ry, 0.6f, rx1, ry, 0.6f, pal::TRACK_HI);
      float u = (float)((now + i * 530) % 1600) / 1600.0f;
      float hx = rx0 + (rx1 - rx0) * u;
      float tx = hx - 26 < rx0 ? rx0 : hx - 26;
      wedge(tx, ry, 0.3f, hx, ry, 1.2f, c, (uint8_t)(255 * sinf(3.14159f * u)));
    } else if (i + 1 < n) {
      wedge(rx0, ry, 0.5f, rx1, ry, 0.5f, ag.status == AStatus::Done ? scale(pal::GREEN, 70) : pal::TRACK_HI);
    }
  }

  sessionDots(f.slot, now, 231);
  if (stale) staleOverlay(s, now);
}

}  // namespace ui
