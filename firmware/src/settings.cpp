#include "settings.h"
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ctype.h>

OrbSettings settings;

static const char* NS = "orb";

static void loadStr(Preferences& p, const char* key, char* dst, size_t len, const char* def) {
  String v = p.getString(key, def);
  strlcpy(dst, v.c_str(), len);
}

static void applyDefaults() {
  memset(&settings, 0, sizeof(settings));
  strlcpy(settings.name, ORB_DEFAULT_NAME, sizeof(settings.name));
  strlcpy(settings.tz, "UTC0", sizeof(settings.tz));
  settings.rotateSec  = 8;
  settings.screenRot  = 0;
  settings.brightness = 200;
  settings.clock24h   = true;
}

void settingsLoad() {
  applyDefaults();
  Preferences p;
  if (!p.begin(NS, true)) return;  // namespace absent on first boot: defaults
  loadStr(p, "ssid",  settings.ssid,  sizeof(settings.ssid),  "");
  loadStr(p, "pass",  settings.pass,  sizeof(settings.pass),  "");
  loadStr(p, "token", settings.token, sizeof(settings.token), "");
  loadStr(p, "name",  settings.name,  sizeof(settings.name),  ORB_DEFAULT_NAME);
  loadStr(p, "tz",    settings.tz,    sizeof(settings.tz),    "UTC0");
  settings.rotateSec  = constrain(p.getUChar("rot", 8), 3, 60);
  settings.screenRot  = p.getUChar("srot", 0) & 3;
  settings.brightness = constrain(p.getUChar("bri", 200), 10, 255);
  settings.clock24h   = p.getBool("h24", true);
  p.end();
  if (!settings.name[0]) strlcpy(settings.name, ORB_DEFAULT_NAME, sizeof(settings.name));
  if (!settings.tz[0])   strlcpy(settings.tz, "UTC0", sizeof(settings.tz));
}

void settingsSave() {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putString("ssid",  settings.ssid);
  p.putString("pass",  settings.pass);
  p.putString("token", settings.token);
  p.putString("name",  settings.name);
  p.putString("tz",    settings.tz);
  p.putUChar("rot",  settings.rotateSec);
  p.putUChar("srot", settings.screenRot);
  p.putUChar("bri",  settings.brightness);
  p.putBool("h24",   settings.clock24h);
  p.end();
}

void settingsFactoryReset() {
  Preferences p;
  if (p.begin(NS, false)) {
    p.clear();
    p.end();
  }
  applyDefaults();
}

bool settingsHasWifi() { return settings.ssid[0] != '\0'; }

void sanitizeHostname(const char* in, char* out, size_t outLen) {
  if (!out || outLen < 2) return;
  size_t o = 0;
  bool lastDash = true;  // suppress leading dash
  for (const char* p = in ? in : ""; *p && o < outLen - 1 && o < 63; ++p) {
    char c = (char)tolower((unsigned char)*p);
    if (isalnum((unsigned char)c)) {
      out[o++] = c;
      lastDash = false;
    } else if (!lastDash) {
      out[o++] = '-';
      lastDash = true;
    }
  }
  while (o > 0 && out[o - 1] == '-') o--;
  out[o] = '\0';
  if (o == 0) strlcpy(out, ORB_DEFAULT_NAME, outLen);
}
