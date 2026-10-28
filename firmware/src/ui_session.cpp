// =============================================================================
//  Session screen (main)
//    outer ring  : context window gauge (green -> amber -> red), compaction
//                  pips in the bottom-left gap
//    inner ring  : 5h rate-limit gauge
//    centre      : ctx label, project, model/effort/mode, animated spark,
//                  turn timer (advanced locally), cost + turn counters
//    bottom gap  : current tool + scrolling detail, session dots
// =============================================================================
#include "ui_common.h"
#include <math.h>

using namespace gfx;

namespace ui {

namespace {
struct Anim { char sid[24]; float ctx, lim; };
Anim anim[ORB_MAX_SESSIONS];

Anim& animFor(const Session& s, int8_t slot) {
  Anim& a = anim[slot < 0 ? 0 : slot];
  if (strcmp(a.sid, s.sid) != 0) {
    strlcpy(a.sid, s.sid, sizeof(a.sid));
    a.ctx = 0;   // sweep up from zero on first show: a nice "boot" for the gauge
    a.lim = 0;
  }
  return a;
}

const char* modeTag(const char* mode, uint16_t& color) {
  if (!strcmp(mode, "plan"))              { color = pal::TEAL;   return "plan"; }
  if (!strcmp(mode, "acceptEdits"))       { color = pal::GREEN;  return "auto-edit"; }
  if (!strcmp(mode, "auto"))              { color = pal::BLUE;   return "auto"; }
  if (!strcmp(mode, "bypassPermissions")) { color = pal::RED;    return "bypass"; }
  return nullptr;
}
}  // namespace

void drawSession(const Session& s, const Frame& f) {
  const uint32_t now = f.now;
  const bool stale = sessionIsStale(s, now);
  const bool busy = s.state != SState::Idle || s.turnRunning;
  Anim& an = animFor(s, f.slot);

  // ---- outer ring: context ----------------------------------------------------
  const bool ctxKnown = !isnan(s.ctxPct);
  an.ctx = approach(an.ctx, ctxKnown ? s.ctxPct : 0, 0.12f);
  const uint16_t cc = ctxColor(an.ctx);
  gauge(113, 9, ctxKnown ? an.ctx : -1, cc, pal::TRACK, true);
  if (ctxKnown && busy && !stale) gaugeShimmer(113, 9, an.ctx, now, cc);

  // Compaction pips: a row of purple ticks tucked under the top of the inner
  // ring, one per compaction (max 6, then a smaller "more" pip).
  {
    uint8_t comp = s.compactions > 6 ? 6 : s.compactions;
    bool more = s.compactions > 6;
    const float gap = 6.5f;
    float x = CX - ((comp + (more ? 1 : 0)) - 1) * gap / 2.0f;
    for (uint8_t i = 0; i < comp; i++, x += gap) disc(x, 27, 1.9f, pal::PURPLE);
    if (more) disc(x, 27, 1.1f, pal::PURPLE, 170);
  }

  // ---- inner ring: 5h limit ------------------------------------------------------
  if (s.lim5h.present) {
    an.lim = approach(an.lim, s.lim5h.pct, 0.12f);
    gauge(100, 4, an.lim, limitColor(an.lim, pal::BLUE), pal::TRACK, false);
  } else {
    gauge(100, 4, -1, pal::BLUE, pal::TRACK, false);
  }

  // ---- top label: context ------------------------------------------------------
  char a[24], b[24], line[64];
  font(F_SMALL);
  if (ctxKnown) {
    snprintf(a, sizeof(a), "%.0f%%", s.ctxPct);
    if (s.ctxTokens) {
      fmtTokens(s.ctxTokens, b, sizeof(b));
      snprintf(line, sizeof(line), " ctx \xC2\xB7 %s", b);
    } else {
      snprintf(line, sizeof(line), " ctx");
    }
    const char* parts[] = {a, line};
    const uint16_t cols[] = {cc, pal::DIM};
    textRuns(parts, cols, 2, CX, 40, F_SMALL, 2 * chordHalf(40, 94));
  } else {
    text("ctx --", (int32_t)CX, 40, pal::FAINT);
  }

  // ---- project + model ------------------------------------------------------------
  font(F_BOLD);
  fitText(s.project, 2 * chordHalf(62, 92), line, sizeof(line));
  text(line, (int32_t)CX, 62, pal::TEXT);

  {
    const char* parts[5];
    uint16_t cols[5];
    int n = 0;
    char modelBuf[48];
    if (s.model[0]) {
      if (s.effort[0]) snprintf(modelBuf, sizeof(modelBuf), "%s \xC2\xB7 %s", s.model, s.effort);
      else strlcpy(modelBuf, s.model, sizeof(modelBuf));
      parts[n] = modelBuf; cols[n++] = pal::DIM;
    }
    uint16_t mc = 0;
    const char* tag = modeTag(s.mode, mc);
    if (tag) {
      if (n) { parts[n] = " \xC2\xB7 "; cols[n++] = pal::FAINT; }
      parts[n] = tag; cols[n++] = mc;
    }
    if (n) textRuns(parts, cols, n, CX, 83, F_SMALL, 2 * chordHalf(83, 94));
  }

  // ---- spark --------------------------------------------------------------------
  const Glyph g = stale ? Glyph::Idle : glyphFor(s.state);
  spark(CX, 118, 19, g, now, stateColor(s.state));

  // ---- timer ---------------------------------------------------------------------
  font(F_DIGITS);
  if (s.turnRunning) {
    fmtClock(sessionTurnElapsed(s, now), line, sizeof(line));
    text(line, (int32_t)CX, 154, pal::TEXT);
  } else if (s.hasLast) {
    fmtClock(s.lastDurationMs, line, sizeof(line));
    text(line, (int32_t)CX, 154, pal::FAINT);
  } else {
    text("0:00", (int32_t)CX, 154, pal::FAINT);
  }

  // ---- stats line ----------------------------------------------------------------
  {
    const char* parts[6];
    uint16_t cols[6];
    int n = 0;
    char cost[16], steps[32], edits[20], errs[16];
    if (!isnan(s.cost)) {
      fmtCost(s.cost, cost, sizeof(cost));
      parts[n] = cost; cols[n++] = pal::TEXT;
    }
    if (s.turnRunning) {
      // Order = priority: textRuns drops trailing runs when the line is full.
      snprintf(steps, sizeof(steps), "%s%u step%s", n ? "  " : "", s.turnSteps, s.turnSteps == 1 ? "" : "s");
      parts[n] = steps; cols[n++] = pal::DIM;
      if (s.turnErrors) {
        snprintf(errs, sizeof(errs), "  %u err", s.turnErrors);
        parts[n] = errs; cols[n++] = pal::RED;
      }
      if (s.turnEdits) {
        snprintf(edits, sizeof(edits), "  %u edit%s", s.turnEdits, s.turnEdits == 1 ? "" : "s");
        parts[n] = edits; cols[n++] = pal::DIM;
      }
    } else if (s.hasLast && !isnan(s.lastCostDelta)) {
      fmtCost(s.lastCostDelta, edits, sizeof(edits), true);
      snprintf(steps, sizeof(steps), "%slast %s", n ? "  " : "", edits);
      parts[n] = steps; cols[n++] = pal::DIM;
    }
    if (n) textRuns(parts, cols, n, CX, 177, F_SMALL, 2 * chordHalf(177, 96));
  }

  // ---- tool line (bottom gap) ------------------------------------------------------
  font(F_BODY);
  const uint16_t sc = stateColor(s.state);
  switch (s.state) {
    case SState::Tool:
    case SState::Error:
    case SState::Waiting: {
      const char* name = s.tool[0] ? s.tool : stateLabel(s.state);
      if (s.state == SState::Error) {
        snprintf(line, sizeof(line), "%s failed", name);
        name = line;
      }
      char fit[48];
      fitText(name, 130, fit, sizeof(fit));
      text(fit, (int32_t)CX, 199, s.state == SState::Tool ? pal::ORANGE_HI : sc);
      break;
    }
    case SState::Thinking: {
      // "thinking" with a soft travelling dot ellipsis
      int w = textWidth("thinking");
      int32_t x0 = (int32_t)(CX - (w + 14) / 2);
      text("thinking", x0, 199, pal::ORANGE_HI, lgfx::middle_left);
      for (int i = 0; i < 3; i++) {
        float ph = sinf(now * 0.008f - i * 0.9f);
        disc(x0 + w + 3 + i * 5, 203, 1.4f, pal::ORANGE_HI, (uint8_t)(110 + 145 * (0.5f + 0.5f * ph)));
      }
      break;
    }
    case SState::Compacting:
      text("compacting", (int32_t)CX, 199, pal::PURPLE);
      break;
    case SState::Idle:
    default:
      text(s.hasLast ? "ready" : "idle", (int32_t)CX, 199, pal::FAINT);
      break;
  }

  // ---- detail marquee ------------------------------------------------------------------
  if (s.state != SState::Idle && s.detail[0]) {
    font(F_SMALL);
    marquee(s.detail, (int32_t)(CX - 64), 216, 128, pal::DIM, now);
  } else if (s.state == SState::Idle && s.logCount) {
    font(F_SMALL);
    marquee(s.log[s.logCount - 1], (int32_t)(CX - 64), 216, 128, pal::FAINT, now);
  }

  sessionDots(f.slot, now, 231);
  if (stale) staleOverlay(s, now);
}

}  // namespace ui
