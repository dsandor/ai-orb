#pragma once
// Shared drawing helpers and per-screen entry points (ui_*.cpp).
#include <Arduino.h>
#include "gfx.h"
#include "sessions.h"

namespace ui {

// Centre of the 240x240 panel (between pixels 119 and 120).
constexpr float CX = 119.5f;
constexpr float CY = 119.5f;

// Gauge sweep shared by every ring: 7:30 -> 4:30 clockwise, gap at the bottom
// for the tool line and session dots.
constexpr float G_START = -135.0f;
constexpr float G_END   = 135.0f;
constexpr float G_SWEEP = 270.0f;

struct Frame {
  uint32_t now;
  int8_t   slot;          // session slot being shown (-1 if n/a)
  uint8_t  page, pages;   // page index / count for the focused session
  bool     multi;         // several busy sessions rotating
};

enum class Glyph : uint8_t { Idle, Thinking, Tool, Waiting, Error, Compacting };

Glyph    glyphFor(SState s);
uint16_t stateColor(SState s);
const char* stateLabel(SState s);
uint16_t ctxColor(float pct);                    // green -> amber -> red
uint16_t limitColor(float pct, uint16_t base);   // base -> amber -> red

// The animated Claude-style spark.
void spark(float cx, float cy, float r, Glyph g, uint32_t t, uint16_t color, uint8_t alpha = 255);
// Track + fill gauge along the shared sweep; pct 0..100, < 0 = unknown.
void gauge(float r, float w, float pct, uint16_t fill, uint16_t track, bool endGlow);
// Moving highlight along [G_START, G_START + sweep*pct] while busy.
void gaugeShimmer(float r, float w, float pct, uint32_t t, uint16_t color);
// Session indicator dots along the bottom (only drawn with >1 session).
void sessionDots(int8_t focusSlot, uint32_t now, float y = 229.0f);
// Draws several coloured text runs centred as one line.
void textRuns(const char* const* parts, const uint16_t* colors, int n, float cx, int32_t y,
              gfx::Font f, int maxW);
// Half-width of the usable chord at height y for content inside radius r.
int  chordHalf(float y, float r);

// Exponential smoothing toward a target (for animated gauges).
float approach(float cur, float target, float k);

// Formatting
void fmtClock(uint32_t ms, char* out, size_t n);        // 0:41 / 12:03 / 1:02:03
void fmtCountdown(int64_t ms, char* out, size_t n);     // 12m / 1h 30m / 4d 2h
void fmtTokens(uint32_t t, char* out, size_t n);        // 850 / 85k / 1.2M
void fmtCost(float c, char* out, size_t n, bool sign = false);  // $1.23 / +$0.21

// Screens
void drawSession(const Session& s, const Frame& f);
void drawAgents(const Session& s, const Frame& f);
void drawLimits(const Session& s, const Frame& f);
void drawAttention(const Session& s, const Frame& f);
void drawDone(const Session& s, const Frame& f, uint32_t startMs, uint32_t durMs);
void drawIdle(const Frame& f);
void drawSetup(const Frame& f);
void drawConnecting(const Frame& f);
void drawConnected(const Frame& f, uint32_t sinceMs);
void drawInfo(const Frame& f);
// Stale overlay: dims the frame and prints how long ago we heard from it.
void staleOverlay(const Session& s, uint32_t now);

}  // namespace ui
