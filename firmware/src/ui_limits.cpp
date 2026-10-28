// =============================================================================
//  Limits / receipt screen
//    outer ring : 5h rate limit      inner ring : 7d rate limit
//    top        : both percentages with reset countdowns (advanced locally)
//    bottom     : last turn receipt (duration, cost delta, edits, errors),
//                 permission gate totals, time-to-first-token
// =============================================================================
#include "ui_common.h"
#include <math.h>

using namespace gfx;

namespace ui {

namespace {
struct LAnim { char sid[24]; float a, b; };
LAnim la[ORB_MAX_SESSIONS];

void limitColumn(const Session& s, const LimitInfo& l, const char* label, float x,
                 uint16_t base, uint32_t now) {
  char pct[12], cd[16], line[32];
  font(F_BOLD);
  if (l.present) {
    snprintf(pct, sizeof(pct), "%.0f%%", l.pct);
    text(pct, (int32_t)x, 68, limitColor(l.pct, base));
  } else {
    text("--", (int32_t)x, 68, pal::FAINT);
  }
  font(F_SMALL);
  text(label, (int32_t)x, 48, pal::DIM);
  if (l.present) {
    int64_t r = sessionResetIn(s, l, now);
    if (r >= 0) {
      fmtCountdown(r, cd, sizeof(cd));
      snprintf(line, sizeof(line), "in %s", cd);
      text(line, (int32_t)x, 89, pal::FAINT);
    }
  }
}
}  // namespace

void drawLimits(const Session& s, const Frame& f) {
  const uint32_t now = f.now;
  const bool stale = sessionIsStale(s, now);
  LAnim& an = la[f.slot < 0 ? 0 : f.slot];
  if (strcmp(an.sid, s.sid) != 0) {
    strlcpy(an.sid, s.sid, sizeof(an.sid));
    an.a = an.b = 0;
  }

  // ---- rings ------------------------------------------------------------------------
  if (s.lim5h.present) {
    an.a = approach(an.a, s.lim5h.pct > 100 ? 100 : s.lim5h.pct, 0.12f);
    gauge(113, 9, an.a, limitColor(an.a, pal::ORANGE), pal::TRACK, true);
  } else {
    gauge(113, 9, -1, pal::ORANGE, pal::TRACK, false);
  }
  if (s.lim7d.present) {
    an.b = approach(an.b, s.lim7d.pct > 100 ? 100 : s.lim7d.pct, 0.12f);
    gauge(100, 6, an.b, limitColor(an.b, pal::BLUE), pal::TRACK, true);
  } else {
    gauge(100, 6, -1, pal::BLUE, pal::TRACK, false);
  }

  // ---- limit columns ------------------------------------------------------------------
  limitColumn(s, s.lim5h, "5 hour", CX - 40, pal::ORANGE, now);
  limitColumn(s, s.lim7d, "7 day", CX + 40, pal::BLUE, now);
  wedge(CX, 50, 0.5f, CX, 90, 0.5f, pal::TRACK_HI);   // column divider

  // ---- receipt -----------------------------------------------------------------------
  wedge(CX - 66, 105, 0.5f, CX + 66, 105, 0.5f, pal::TRACK_HI);
  font(F_SMALL);
  char line[48], a[16], b[16];
  if (s.hasLast) {
    text("last turn", (int32_t)CX, 118, pal::DIM);
    font(F_DIGITS);
    fmtClock(s.lastDurationMs, line, sizeof(line));
    text(line, (int32_t)CX, 143, pal::TEXT);

    const char* parts[5];
    uint16_t cols[5];
    int n = 0;
    fmtCost(s.lastCostDelta, a, sizeof(a), true);
    parts[n] = a; cols[n++] = isnan(s.lastCostDelta) ? pal::FAINT : pal::TEXT;
    snprintf(b, sizeof(b), "  %u edit%s", s.lastEdits, s.lastEdits == 1 ? "" : "s");
    parts[n] = b; cols[n++] = pal::DIM;
    char e[16];
    snprintf(e, sizeof(e), "  %u err", s.lastErrors);
    parts[n] = e; cols[n++] = s.lastErrors ? pal::RED : pal::DIM;
    textRuns(parts, cols, n, CX, 168, F_SMALL, 2 * chordHalf(168, 96));
  } else {
    text("no completed turn yet", (int32_t)CX, 140, pal::FAINT);
  }

  // ---- gate + ttft ---------------------------------------------------------------------
  {
    char g1[16], g2[16], g3[16], g4[20];
    snprintf(g1, sizeof(g1), "%u allow", s.gateAllowed);
    snprintf(g2, sizeof(g2), "  %u ask", s.gateAsked);
    snprintf(g3, sizeof(g3), "  %u deny", s.gateDenied);
    const char* parts[4] = {g1, g2, g3, nullptr};
    uint16_t cols[4] = {pal::GREEN, pal::AMBER, s.gateDenied ? pal::RED : pal::DIM, pal::AMBER};
    int n = 3;
    if (s.gatePending) {
      snprintf(g4, sizeof(g4), "  %u wait", s.gatePending);
      parts[n++] = g4;
    }
    textRuns(parts, cols, n, CX, 186, F_SMALL, 2 * chordHalf(186, 95));
  }
  font(F_SMALL);
  {
    char t[20], c[16];
    fmtCost(s.cost, c, sizeof(c));
    if (s.ttftMs >= 0) snprintf(t, sizeof(t), "ttft %ld ms", (long)s.ttftMs);
    else strlcpy(t, "ttft --", sizeof(t));
    snprintf(line, sizeof(line), "%s \xC2\xB7 %s total", t, c);
    text(line, (int32_t)CX, 207, pal::FAINT);
  }

  sessionDots(f.slot, now, 231);
  if (stale) staleOverlay(s, now);
}

}  // namespace ui
