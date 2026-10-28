#pragma once
// =============================================================================
//  Claude Orb - compile-time configuration
//  Pins and panel options come from platformio.ini build flags; the defaults
//  here only exist so a bare build still compiles (ESP32-S3 Super Mini map).
// =============================================================================
#include <stdint.h>

#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"
#endif
#ifndef BOARD_NAME
#define BOARD_NAME "unknown"
#endif

// --- Display wiring -----------------------------------------------------------
#ifndef PIN_SCLK
#define PIN_SCLK 12
#endif
#ifndef PIN_MOSI
#define PIN_MOSI 11
#endif
#ifndef PIN_DC
#define PIN_DC 9
#endif
#ifndef PIN_CS
#define PIN_CS 10
#endif
#ifndef PIN_RST
#define PIN_RST 8
#endif
#ifndef PIN_BL
#define PIN_BL -1          // -1 = backlight hard-wired on
#endif
#ifndef PIN_BUTTON
#define PIN_BUTTON -1      // -1 = no button
#endif
#ifndef PANEL_INVERT
#define PANEL_INVERT 1
#endif
#ifndef PANEL_RGB_ORDER
#define PANEL_RGB_ORDER 0
#endif
#ifndef PANEL_SPI_FREQ
#define PANEL_SPI_FREQ 40000000
#endif

// --- Network ---------------------------------------------------------------------
#define ORB_DEFAULT_NAME   "claude-orb"
#define ORB_AP_SSID        "Claude-Orb-Setup"
#define ORB_HTTP_PORT      80
#define ORB_STA_TIMEOUT_MS 20000UL   // give up on a boot STA connect after this
#define ORB_STA_RETRY_MS   60000UL   // retry stored creds from AP mode this often
#define ORB_MAX_BODY       8192      // largest /api/state body we accept

// --- Protocol (docs/protocol.md) ---------------------------------------------------
#define ORB_PROTO_VERSION  1
#define ORB_MAX_SESSIONS   4
#define ORB_MAX_AGENTS     4
#define ORB_MAX_LOG        4
#define ORB_STALE_MS       45000UL    // dim after 45 s of silence
#define ORB_DROP_MS        300000UL   // drop after 5 min of silence
#define ORB_ROTATE_MS      10000UL    // busy-session rotation period

// --- UI -------------------------------------------------------------------------
#define ORB_SCREEN_W       240
#define ORB_SCREEN_H       240
#define ORB_FPS_ACTIVE     30
#define ORB_FPS_IDLE       15
#define ORB_DONE_FLASH_MS  3200UL     // turn-complete celebration length
#define ORB_IDLE_CLOCK_MS  120000UL   // all sessions idle this long -> clock
#define ORB_MANUAL_HOLD_MS 30000UL    // button press pins the view this long
#define ORB_LONG_PRESS_MS  700UL
