#pragma once
// Persistent device settings (NVS via Preferences, namespace "orb").
#include <stdint.h>
#include <stddef.h>

struct OrbSettings {
  char    ssid[33];
  char    pass[65];
  char    token[65];      // empty = no auth
  char    name[32];       // device / mDNS host name
  char    tz[64];         // POSIX TZ string, e.g. "EST5EDT,M3.2.0,M11.1.0"
  uint8_t rotateSec;      // page auto-rotate interval, 3..60
  uint8_t screenRot;      // 0..3 (x90 degrees)
  uint8_t brightness;     // 10..255, only used when PIN_BL >= 0
  bool    clock24h;
};

extern OrbSettings settings;

void settingsLoad();
void settingsSave();
void settingsFactoryReset();      // wipes NVS namespace (wifi included)
bool settingsHasWifi();

// Turns an arbitrary name into a valid DNS label ("AI Orb!" -> "ai-orb").
void sanitizeHostname(const char* in, char* out, size_t outLen);
