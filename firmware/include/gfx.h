#pragma once
// =============================================================================
//  gfx: GC9A01 panel + full-frame canvas + anti-aliased primitives + fonts.
//
//  Coordinate conventions used by every primitive here:
//    - pixel (x, y) has its centre at (x, y)
//    - angles are DEGREES, 0 = 12 o'clock, increasing CLOCKWISE
//    - colours are RGB565; alpha is 0..255 and blends onto what is already
//      in the canvas, so primitives compose (glows, overlapping arcs, text).
// =============================================================================
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace gfx {

// ---- colour helpers ---------------------------------------------------------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t hex(uint32_t c) {
  return rgb((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}
// t = 0 -> a, t = 255 -> b
uint16_t mix(uint16_t a, uint16_t b, uint8_t t);
// Scales brightness, level 255 = unchanged.
uint16_t scale(uint16_t c, uint8_t level);

// ---- palette ------------------------------------------------------------------
namespace pal {
constexpr uint16_t BG        = hex(0x000000);
constexpr uint16_t TRACK     = hex(0x1E1C22);
constexpr uint16_t TRACK_HI  = hex(0x2C2930);
constexpr uint16_t TEXT      = hex(0xF4EFEA);
constexpr uint16_t DIM       = hex(0x9A938C);
constexpr uint16_t FAINT     = hex(0x55504B);
constexpr uint16_t ORANGE    = hex(0xD97757);   // Claude accent
constexpr uint16_t ORANGE_HI = hex(0xF2A585);
constexpr uint16_t GREEN     = hex(0x5ED38C);
constexpr uint16_t AMBER     = hex(0xF4B544);
constexpr uint16_t RED       = hex(0xF0574F);
constexpr uint16_t BLUE      = hex(0x7AA2F7);
constexpr uint16_t PURPLE    = hex(0xB59AF5);
constexpr uint16_t TEAL      = hex(0x52D6C8);
}  // namespace pal

// ---- fonts --------------------------------------------------------------------
enum Font : uint8_t {
  F_SMALL = 0,   // Inter Regular 12 px
  F_BODY,        // Inter Regular 16 px
  F_BOLD,        // Inter Bold 22 px
  F_DIGITS,      // Inter Bold 32 px, " -.0123456789:" only
  F_CLOCK,       // Inter Bold 60 px, " -.0123456789:" only
  F_COUNT
};

// ---- lifecycle ----------------------------------------------------------------
bool  begin(uint8_t rotation, uint8_t brightness);
void  setRotation(uint8_t rotation);
void  setBrightness(uint8_t level);     // no-op without a backlight pin
void  present();                        // push the canvas to the panel
LGFX_Sprite& canvas();
const char* canvasInfo();               // "PSRAM 16bpp" etc., for logs/info

// ---- primitives (draw into the canvas) ----------------------------------------
void clear(uint16_t color = pal::BG);
void blendPixel(int32_t x, int32_t y, uint16_t color, uint8_t alpha);
// Arc along centre-line radius r with stroke width w from a0 to a1 degrees.
void arc(float cx, float cy, float r, float w, float a0, float a1,
         uint16_t color, uint8_t alpha = 255, bool roundCaps = true);
void ring(float cx, float cy, float r, float w, uint16_t color, uint8_t alpha = 255);
void disc(float cx, float cy, float r, uint16_t color, uint8_t alpha = 255);
// Soft radial glow: alpha falls off quadratically from centre to radius.
void glow(float cx, float cy, float r, uint16_t color, uint8_t alpha);
// Glow band centred on radius r with half-width hw (soft both sides).
void glowRing(float cx, float cy, float r, float hw, uint16_t color, uint8_t alpha);
// Tapered capsule from (ax,ay) radius ar to (bx,by) radius br.
void wedge(float ax, float ay, float ar, float bx, float by, float br,
           uint16_t color, uint8_t alpha = 255);
// Darkens the whole canvas (level 255 = unchanged, 0 = black).
void fade(uint8_t level);

// Point on a circle for the angle convention above.
void polar(float cx, float cy, float r, float deg, float& x, float& y);

// ---- text -----------------------------------------------------------------------
void font(Font f);
// Draws UTF-8 text with the given datum (e.g. middle_center), blending onto
// the canvas. Returns rendered width.
int  text(const char* s, int32_t x, int32_t y, uint16_t color,
          lgfx::textdatum_t datum = lgfx::middle_center);
int  textWidth(const char* s);
// Copies `s` into `out`, cutting it with "..." so it fits in maxW px.
void fitText(const char* s, int maxW, char* out, size_t outLen);
// Horizontally scrolling text clipped to [x, x+w): ping-pong marquee when it
// does not fit, centred otherwise. `t` = millis().
void marquee(const char* s, int32_t x, int32_t y, int32_t w, uint16_t color, uint32_t t);

}  // namespace gfx
