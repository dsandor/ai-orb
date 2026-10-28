// =============================================================================
//  gfx: panel driver, full-frame canvas and anti-aliased primitives.
//  Panel setup follows the BambuHelper esp32s3_round env (LovyanGFX GC9A01).
//  The AA rasterisers write straight into the 16-bit sprite buffer and only
//  visit pixels inside each shape's row spans, so a full gauge ring costs a few
//  thousand pixel evaluations rather than a 240x240 scan.
// =============================================================================
#include <Arduino.h>
#include "gfx.h"
#include "config.h"
#include <math.h>
#include <new>

#include "fonts/inter_10.h"
#include "fonts/inter_14.h"
#include "fonts/inter_19.h"
#include "fonts/inter_card_mid.h"
#include "fonts/inter_card_num.h"

namespace {

#if defined(ORB_HOST_PREVIEW)
// Desktop preview build (firmware/preview): same canvas + primitives, no panel.
LGFX_Sprite spr;
#else
class LGFX_Orb : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI      _bus;
#if PIN_BL >= 0
  lgfx::Light_PWM    _light;
#endif
 public:
  LGFX_Orb() {
    {
      auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = PANEL_SPI_FREQ;
      cfg.freq_read   = 16000000;
      cfg.pin_sclk    = PIN_SCLK;
      cfg.pin_mosi    = PIN_MOSI;
      cfg.pin_miso    = -1;
      cfg.pin_dc      = PIN_DC;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs        = PIN_CS;
      cfg.pin_rst       = PIN_RST;
      cfg.pin_busy      = -1;
      cfg.memory_width  = 240;
      cfg.memory_height = 240;
      cfg.panel_width   = 240;
      cfg.panel_height  = 240;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;
      cfg.readable      = false;
      cfg.invert        = PANEL_INVERT;
      cfg.rgb_order     = PANEL_RGB_ORDER;
      _panel.config(cfg);
    }
#if PIN_BL >= 0
    {
      auto cfg = _light.config();
      cfg.pin_bl      = PIN_BL;
      cfg.invert      = false;
      cfg.freq        = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
#endif
    setPanel(&_panel);
  }
};

LGFX_Orb    tft;
LGFX_Sprite spr(&tft);
#endif
uint16_t*   fb = nullptr;      // byte-swapped RGB565 when fast path active
char        info[32] = "none";

constexpr int W = ORB_SCREEN_W;
constexpr int H = ORB_SCREEN_H;
constexpr float PI_F  = 3.14159265358979f;
constexpr float TAU_F = 6.28318530717959f;

// --- fonts (parsed once, selected by pointer) ---
const uint8_t* const fontBlobs[gfx::F_COUNT] = {
    inter_10, inter_14, inter_19, inter_card_mid, inter_card_num};
lgfx::PointerWrapper* fontSrc[gfx::F_COUNT] = {};
lgfx::VLWfont*        fontObj[gfx::F_COUNT] = {};

inline uint16_t sw16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

inline uint16_t blend565(uint16_t bg, uint16_t fg, uint32_t a) {  // a 0..256
  // Spread 565 into 0x07E0F81F lanes so all three channels blend in one mul.
  // Weights are 5-bit (0..32) so the widest lane (green, 6 bits) cannot
  // overflow into its neighbour: 6 + 5 bits fit in bits 21..31.
  const uint32_t w = a >> 3;
  uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81Fu;
  uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81Fu;
  uint32_t r = ((f * w + b * (32u - w)) >> 5) & 0x07E0F81Fu;
  return (uint16_t)(r | (r >> 16));
}

inline void plot(int32_t x, int32_t y, uint16_t color, float cov, uint8_t alpha) {
  if (cov <= 0.0f) return;
  uint32_t a = (uint32_t)(cov * (float)alpha + 0.5f);
  if (a == 0) return;
  if (a > 255) a = 255;
  if ((uint32_t)x >= (uint32_t)W || (uint32_t)y >= (uint32_t)H) return;
  if (fb) {
    uint16_t* p = fb + y * W + x;
    if (a >= 255) { *p = sw16(color); return; }
    *p = sw16(blend565(sw16(*p), color, a + 1));
  } else {
    if (a >= 255) { spr.drawPixel(x, y, color); return; }
    uint16_t bg = spr.readPixel(x, y);
    spr.drawPixel(x, y, blend565(bg, color, a + 1));
  }
}

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// atan2 with ~1e-5 rad error; plenty for sub-pixel arc ends at r = 120.
inline float fastAtan2(float y, float x) {
  float ax = fabsf(x), ay = fabsf(y);
  float mn = ax < ay ? ax : ay, mx = ax < ay ? ay : ax;
  if (mx == 0.0f) return 0.0f;
  float a = mn / mx, s = a * a;
  float r = ((((-0.0117212f * s + 0.05265332f) * s - 0.11643287f) * s + 0.19354346f) * s
             - 0.33262347f) * s * a + 0.99997726f * a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0)   r = PI_F - r;
  if (y < 0)   r = -r;
  return r;
}

// Clockwise angle from 12 o'clock, radians, [0, 2pi).
inline float clockAngle(float dx, float dy) {
  float t = fastAtan2(dx, -dy);
  return t < 0 ? t + TAU_F : t;
}

inline int32_t clampRow(int32_t v) { return v < 0 ? 0 : (v >= H ? H - 1 : v); }

}  // namespace

namespace gfx {

uint16_t mix(uint16_t a, uint16_t b, uint8_t t) { return blend565(a, b, (uint32_t)t + 1); }

uint16_t scale(uint16_t c, uint8_t level) { return blend565(0, c, (uint32_t)level + 1); }

LGFX_Sprite& canvas() { return spr; }
const char* canvasInfo() { return info; }

bool begin(uint8_t rotation, uint8_t brightness) {
#if !defined(ORB_HOST_PREVIEW)
  tft.init();
  tft.setRotation(rotation & 3);
  tft.setBrightness(brightness);
  tft.fillScreen(TFT_BLACK);
#endif

  // Full-frame canvas: PSRAM when present (keeps internal heap for Wi-Fi /
  // HTTP), then internal SRAM, then an 8-bit fallback (slow path blending).
  spr.setColorDepth(16);
  void* buf = nullptr;
#if defined(ORB_CANVAS_SRAM_FIRST) && ORB_CANVAS_SRAM_FIRST
  spr.setPsram(false);
  buf = spr.createSprite(W, H);
  if (buf) strlcpy(info, "SRAM 16bpp", sizeof(info));
#endif
  if (!buf && psramFound()) {
    spr.setPsram(true);
    buf = spr.createSprite(W, H);
    if (buf) strlcpy(info, "PSRAM 16bpp", sizeof(info));
  }
  if (!buf) {
    spr.setPsram(false);
    buf = spr.createSprite(W, H);
    if (buf) strlcpy(info, "SRAM 16bpp", sizeof(info));
  }
  if (!buf) {
    spr.setColorDepth(8);
    buf = spr.createSprite(W, H);
    if (buf) strlcpy(info, "SRAM 8bpp", sizeof(info));
  }
  if (!buf) {
    strlcpy(info, "no canvas", sizeof(info));
    return false;
  }
  fb = (spr.getColorDepth() == 16) ? (uint16_t*)buf : nullptr;
  spr.setTextWrap(false);
  spr.fillScreen(TFT_BLACK);

  for (int i = 0; i < F_COUNT; i++) {
    auto* w = new (std::nothrow) lgfx::PointerWrapper();
    auto* f = new (std::nothrow) lgfx::VLWfont();
    if (w && f) {
      w->set(fontBlobs[i]);
      if (f->loadFont(w)) { fontSrc[i] = w; fontObj[i] = f; continue; }
    }
    delete f;
    delete w;
  }
  return true;
}

#if defined(ORB_HOST_PREVIEW)
void setRotation(uint8_t) {}
void setBrightness(uint8_t) {}
void present() {}
#else
void setRotation(uint8_t rotation) { tft.setRotation(rotation & 3); }
void setBrightness(uint8_t level) { tft.setBrightness(level); }

void present() {
  tft.startWrite();
  spr.pushSprite(0, 0);
  tft.endWrite();
}
#endif

void clear(uint16_t color) { spr.fillScreen(color); }

void blendPixel(int32_t x, int32_t y, uint16_t color, uint8_t alpha) {
  plot(x, y, color, 1.0f, alpha);
}

void polar(float cx, float cy, float r, float deg, float& x, float& y) {
  float a = deg * (PI_F / 180.0f);
  x = cx + r * sinf(a);
  y = cy - r * cosf(a);
}

void arc(float cx, float cy, float r, float w, float a0, float a1,
         uint16_t color, uint8_t alpha, bool roundCaps) {
  if (w <= 0.0f || r <= 0.0f || alpha == 0) return;
  if (a1 < a0) { float t = a0; a0 = a1; a1 = t; }
  float spanDeg = a1 - a0;
  bool full = spanDeg >= 359.99f;
  const float hw = w * 0.5f;
  const float ro = r + hw + 1.0f, ri = r - hw - 1.0f;

  float s0 = fmodf(a0, 360.0f);
  if (s0 < 0) s0 += 360.0f;
  const float start = s0 * (PI_F / 180.0f);
  const float span  = spanDeg * (PI_F / 180.0f);
  float c0x, c0y, c1x, c1y;
  polar(cx, cy, r, a0, c0x, c0y);
  polar(cx, cy, r, a1, c1x, c1y);

  // Bounding box of the arc: its end caps plus every cardinal extreme
  // (12/3/6/9 o'clock) the span passes through. Short arcs (comets,
  // shimmers, segments) then touch only their own neighbourhood.
  float bx0 = cx - ro, bx1 = cx + ro, by0 = cy - ro, by1 = cy + ro;
  if (!full) {
    bx0 = by0 = 1e9f;
    bx1 = by1 = -1e9f;
    auto grow = [&](float x, float y, float pad) {
      if (x - pad < bx0) bx0 = x - pad;
      if (x + pad > bx1) bx1 = x + pad;
      if (y - pad < by0) by0 = y - pad;
      if (y + pad > by1) by1 = y + pad;
    };
    grow(c0x, c0y, hw + 1.5f);
    grow(c1x, c1y, hw + 1.5f);
    for (int k = 0; k < 4; k++) {
      float rel = k * 90.0f - s0;
      if (rel < 0) rel += 360.0f;
      if (rel <= spanDeg) {
        float px, py;
        polar(cx, cy, r, k * 90.0f, px, py);
        grow(px, py, hw + 1.5f);
      }
    }
  }
  const int32_t bxa = (int32_t)floorf(bx0), bxb = (int32_t)ceilf(bx1);

  int32_t y0 = clampRow((int32_t)floorf(by0 > cy - ro ? by0 : cy - ro));
  int32_t y1 = clampRow((int32_t)ceilf(by1 < cy + ro ? by1 : cy + ro));
  for (int32_t y = y0; y <= y1; y++) {
    float dy = (float)y - cy;
    float dy2 = dy * dy;
    if (dy2 > ro * ro) continue;
    float xo = sqrtf(ro * ro - dy2);
    float xi = (ri > 0 && dy2 < ri * ri) ? sqrtf(ri * ri - dy2) : -1.0f;
    // Up to two horizontal spans per row: [-xo, -xi] and [xi, xo].
    for (int side = 0; side < 2; side++) {
      int32_t xa, xb;
      if (xi < 0) {
        if (side) break;
        xa = (int32_t)floorf(cx - xo);
        xb = (int32_t)ceilf(cx + xo);
      } else if (side == 0) {
        xa = (int32_t)floorf(cx - xo);
        xb = (int32_t)ceilf(cx - xi);
      } else {
        xa = (int32_t)floorf(cx + xi);
        xb = (int32_t)ceilf(cx + xo);
      }
      if (xa < bxa) xa = bxa;
      if (xb > bxb) xb = bxb;
      if (xa < 0) xa = 0;
      if (xb > W - 1) xb = W - 1;
      for (int32_t x = xa; x <= xb; x++) {
        float dx = (float)x - cx;
        float d = sqrtf(dx * dx + dy2);
        float sd = fabsf(d - r) - hw;
        if (!full) {
          float rel = clockAngle(dx, dy) - start;
          if (rel < 0) rel += TAU_F;
          if (rel > span) {
            if (roundCaps) {
              float e0x = x - c0x, e0y = y - c0y, e1x = x - c1x, e1y = y - c1y;
              float d0 = e0x * e0x + e0y * e0y, d1 = e1x * e1x + e1y * e1y;
              sd = sqrtf(d0 < d1 ? d0 : d1) - hw;
            } else {
              float out = rel - span, back = TAU_F - rel;
              float da = (out < back ? out : back) * d;
              if (da > sd) sd = da;
            }
          }
        }
        plot(x, y, color, clamp01(0.5f - sd), alpha);
      }
    }
  }
}

void ring(float cx, float cy, float r, float w, uint16_t color, uint8_t alpha) {
  arc(cx, cy, r, w, 0.0f, 360.0f, color, alpha, false);
}

void disc(float cx, float cy, float r, uint16_t color, uint8_t alpha) {
  if (r <= 0 || alpha == 0) return;
  const float ro = r + 1.0f;
  int32_t y0 = clampRow((int32_t)floorf(cy - ro)), y1 = clampRow((int32_t)ceilf(cy + ro));
  for (int32_t y = y0; y <= y1; y++) {
    float dy = (float)y - cy, dy2 = dy * dy;
    if (dy2 > ro * ro) continue;
    float xo = sqrtf(ro * ro - dy2);
    int32_t xa = (int32_t)floorf(cx - xo), xb = (int32_t)ceilf(cx + xo);
    if (xa < 0) xa = 0;
    if (xb > W - 1) xb = W - 1;
    for (int32_t x = xa; x <= xb; x++) {
      float dx = (float)x - cx;
      plot(x, y, color, clamp01(r + 0.5f - sqrtf(dx * dx + dy2)), alpha);
    }
  }
}

void glow(float cx, float cy, float r, uint16_t color, uint8_t alpha) {
  if (r <= 0 || alpha == 0) return;
  int32_t y0 = clampRow((int32_t)floorf(cy - r)), y1 = clampRow((int32_t)ceilf(cy + r));
  const float inv = 1.0f / r;
  for (int32_t y = y0; y <= y1; y++) {
    float dy = (float)y - cy, dy2 = dy * dy;
    if (dy2 >= r * r) continue;
    float xo = sqrtf(r * r - dy2);
    int32_t xa = (int32_t)floorf(cx - xo), xb = (int32_t)ceilf(cx + xo);
    if (xa < 0) xa = 0;
    if (xb > W - 1) xb = W - 1;
    for (int32_t x = xa; x <= xb; x++) {
      float dx = (float)x - cx;
      float k = 1.0f - sqrtf(dx * dx + dy2) * inv;
      if (k > 0) plot(x, y, color, k * k, alpha);
    }
  }
}

void glowRing(float cx, float cy, float r, float hw, uint16_t color, uint8_t alpha) {
  if (hw <= 0 || alpha == 0) return;
  const float ro = r + hw, ri = r - hw;
  const float inv = 1.0f / hw;
  int32_t y0 = clampRow((int32_t)floorf(cy - ro)), y1 = clampRow((int32_t)ceilf(cy + ro));
  for (int32_t y = y0; y <= y1; y++) {
    float dy = (float)y - cy, dy2 = dy * dy;
    if (dy2 > ro * ro) continue;
    float xo = sqrtf(ro * ro - dy2);
    float xi = (ri > 0 && dy2 < ri * ri) ? sqrtf(ri * ri - dy2) : -1.0f;
    for (int side = 0; side < 2; side++) {
      int32_t xa, xb;
      if (xi < 0) {
        if (side) break;
        xa = (int32_t)floorf(cx - xo); xb = (int32_t)ceilf(cx + xo);
      } else if (side == 0) {
        xa = (int32_t)floorf(cx - xo); xb = (int32_t)ceilf(cx - xi);
      } else {
        xa = (int32_t)floorf(cx + xi); xb = (int32_t)ceilf(cx + xo);
      }
      if (xa < 0) xa = 0;
      if (xb > W - 1) xb = W - 1;
      for (int32_t x = xa; x <= xb; x++) {
        float dx = (float)x - cx;
        float k = 1.0f - fabsf(sqrtf(dx * dx + dy2) - r) * inv;
        if (k > 0) plot(x, y, color, k * k, alpha);
      }
    }
  }
}

void wedge(float ax, float ay, float ar, float bx, float by, float br,
           uint16_t color, uint8_t alpha) {
  if (ar < 0 || br < 0 || alpha == 0) return;
  if (fabsf(ax - bx) < 0.01f && fabsf(ay - by) < 0.01f) bx += 0.01f;
  int32_t x0 = (int32_t)floorf(fminf(ax - ar, bx - br)) - 1;
  int32_t x1 = (int32_t)ceilf(fmaxf(ax + ar, bx + br)) + 1;
  int32_t y0 = (int32_t)floorf(fminf(ay - ar, by - br)) - 1;
  int32_t y1 = (int32_t)ceilf(fmaxf(ay + ar, by + br)) + 1;
  if (x1 < 0 || y1 < 0 || x0 >= W || y0 >= H) return;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > W - 1) x1 = W - 1;
  if (y1 > H - 1) y1 = H - 1;
  const float bax = bx - ax, bay = by - ay;
  const float denom = bax * bax + bay * bay;
  const float dr = ar - br;
  for (int32_t y = y0; y <= y1; y++) {
    float py = y - ay;
    for (int32_t x = x0; x <= x1; x++) {
      float px = x - ax;
      float h = (px * bax + py * bay) / denom;
      h = h < 0 ? 0 : (h > 1 ? 1 : h);
      float ex = px - bax * h, ey = py - bay * h;
      float dist = sqrtf(ex * ex + ey * ey) + h * dr;   // distance minus taper
      plot(x, y, color, clamp01(ar + 0.5f - dist), alpha);
    }
  }
}

void fade(uint8_t level) {
  if (level >= 255) return;
  if (fb) {
    const uint32_t a = (uint32_t)level + 1;
    for (int i = 0; i < W * H; i++) {
      uint16_t v = fb[i];
      if (v) fb[i] = sw16(blend565(0, sw16(v), a));
    }
  } else {
    // 8-bit fallback: approximate with a translucent black overlay.
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) plot(x, y, 0, 1.0f, 255 - level);
  }
}

// ---- text ------------------------------------------------------------------------
void font(Font f) {
  if (f < F_COUNT && fontObj[f]) {
    spr.setFont(fontObj[f]);
  } else {
    spr.setFont(f >= F_BOLD ? &fonts::Font4 : &fonts::Font2);
  }
  spr.setTextSize(1);
}

int text(const char* s, int32_t x, int32_t y, uint16_t color, lgfx::textdatum_t datum) {
  if (!s || !s[0]) return 0;
  spr.setTextDatum(datum);
  spr.setTextColor(color);   // fg only: smooth font blends onto the canvas
  return spr.drawString(s, x, y);
}

int textWidth(const char* s) { return (s && s[0]) ? spr.textWidth(s) : 0; }

void fitText(const char* s, int maxW, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  strlcpy(out, s ? s : "", outLen);
  if (spr.textWidth(out) <= maxW) return;
  size_t n = strlen(out);
  while (n > 0) {
    n--;
    while (n > 0 && ((uint8_t)out[n] & 0xC0) == 0x80) n--;   // UTF-8 boundary
    if (n + 4 > outLen) continue;
    out[n] = '\0';
    while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
    strlcat(out, "...", outLen);
    if (spr.textWidth(out) <= maxW) return;
    out[n] = '\0';
  }
  strlcpy(out, "...", outLen);
}

void marquee(const char* s, int32_t x, int32_t y, int32_t w, uint16_t color, uint32_t t) {
  if (!s || !s[0]) return;
  int tw = spr.textWidth(s);
  if (tw <= w) {
    text(s, x + w / 2, y, color, lgfx::middle_center);
    return;
  }
  // Ping-pong: hold 1.5 s at each end, scroll at 30 px/s in between.
  const int travel = tw - w;
  const uint32_t hold = 1500, move = (uint32_t)travel * 1000 / 30;
  const uint32_t period = 2 * (hold + move);
  uint32_t p = t % period;
  int off;
  if (p < hold) off = 0;
  else if (p < hold + move) off = (int)((p - hold) * 30 / 1000);
  else if (p < 2 * hold + move) off = travel;
  else off = travel - (int)((p - 2 * hold - move) * 30 / 1000);
  if (off > travel) off = travel;
  if (off < 0) off = 0;
  spr.setClipRect(x, y - 16, w, 32);
  text(s, x - off, y, color, lgfx::middle_left);
  spr.clearClipRect();
  // Soft fade at both edges so the cut is not harsh.
  for (int i = 0; i < 10; i++) {
    uint8_t a = (uint8_t)(255 - i * 25);
    for (int yy = y - 10; yy <= y + 10; yy++) {
      if (off > 0) plot(x + i, yy, pal::BG, 1.0f, a);
      if (off < travel) plot(x + w - 1 - i, yy, pal::BG, 1.0f, a);
    }
  }
}

}  // namespace gfx
