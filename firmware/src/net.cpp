// =============================================================================
//  Networking: STA connect with captive-portal AP fallback (pattern borrowed
//  from BambuHelper's wifi_manager), mDNS advertisement and NTP.
// =============================================================================
#include "net.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <time.h>

static NetState  state = NetState::Connecting;
static DNSServer dns;
static bool      dnsUp = false;
static uint32_t  connectStart = 0;
static uint32_t  lastRetry = 0;
static bool      retrying = false;
static uint32_t  restartAt = 0;
static volatile bool gotIp = false;
static char      ipStr[16] = "0.0.0.0";
static char      host[40] = ORB_DEFAULT_NAME;

static void onWifiEvent(arduino_event_id_t ev, arduino_event_info_t) {
  if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP) gotIp = true;
}

static void startSta() {
  WiFi.setHostname(host);
  WiFi.setAutoReconnect(true);
  WiFi.begin(settings.ssid, settings.pass);
  connectStart = millis();
}

static void startMdns() {
  MDNS.end();
  if (!MDNS.begin(host)) {
    Serial.println("[net] mDNS start failed");
    return;
  }
  MDNS.setInstanceName(host);
  // _claude-orb._tcp is how the Claude Code mod finds orbs (/orb choose); _http._tcp is for browsers
  // and older mods. Both carry the name and IP: when two orbs share a name, mDNS renames one of
  // them on the wire (claude-orb-2.local) and the mod has to reach that one by its IP.
  for (const char* svc : {"claude-orb", "http"}) {
    MDNS.addService(svc, "tcp", ORB_HTTP_PORT);
    MDNS.addServiceTxt(svc, "tcp", "fw", FW_VERSION);
    MDNS.addServiceTxt(svc, "tcp", "proto", "1");
    MDNS.addServiceTxt(svc, "tcp", "api", "/api/state");
    MDNS.addServiceTxt(svc, "tcp", "name", host);
    MDNS.addServiceTxt(svc, "tcp", "ip", ipStr);
  }
  Serial.printf("[net] mDNS http://%s.local\n", host);
}

static void startAp() {
  WiFi.mode(WIFI_AP_STA);       // STA side stays available for scans/retries
  WiFi.softAP(ORB_AP_SSID);     // open network: setup happens at arm's length
  delay(100);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dnsUp = dns.start(53, "*", WiFi.softAPIP());
  strlcpy(ipStr, WiFi.softAPIP().toString().c_str(), sizeof(ipStr));
  state = NetState::Setup;
  lastRetry = millis();
  retrying = false;
  netStartApScan();
  Serial.printf("[net] setup AP '%s' at %s\n", ORB_AP_SSID, ipStr);
}

static void stopAp() {
  if (dnsUp) { dns.stop(); dnsUp = false; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

static void onConnected() {
  strlcpy(ipStr, WiFi.localIP().toString().c_str(), sizeof(ipStr));
  if (state == NetState::Setup) stopAp();
  state = NetState::Connected;
  configTzTime(settings.tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  startMdns();
  Serial.printf("[net] connected to '%s' ip %s rssi %d\n", settings.ssid, ipStr, WiFi.RSSI());
}

void netBegin() {
  sanitizeHostname(settings.name, host, sizeof(host));
  WiFi.onEvent(onWifiEvent);
  WiFi.persistent(false);       // creds live in our own NVS namespace
  setenv("TZ", settings.tz, 1);
  tzset();
  if (settingsHasWifi()) {
    WiFi.mode(WIFI_STA);
    state = NetState::Connecting;
    startSta();
    Serial.printf("[net] connecting to '%s'\n", settings.ssid);
  } else {
    startAp();
  }
}

void netLoop(uint32_t now) {
  if (restartAt && (int32_t)(now - restartAt) >= 0) {
    Serial.println("[net] restarting");
    delay(50);
    ESP.restart();
  }
  if (dnsUp) dns.processNextRequest();

  switch (state) {
    case NetState::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        gotIp = false;
        onConnected();
      } else if (now - connectStart > ORB_STA_TIMEOUT_MS) {
        Serial.println("[net] STA connect timed out -> setup AP");
        WiFi.disconnect(false, false);
        startAp();
      }
      break;

    case NetState::Connected:
      if (gotIp) {               // re-associated after a drop: refresh IP + mDNS
        gotIp = false;
        strlcpy(ipStr, WiFi.localIP().toString().c_str(), sizeof(ipStr));
        startMdns();
      }
      break;

    case NetState::Setup:
      // Periodically retry the stored network (router rebooted, etc.), but
      // only while nobody is using the portal - an STA attempt can hop the
      // AP channel under a connected phone.
      if (settingsHasWifi()) {
        if (retrying) {
          if (WiFi.status() == WL_CONNECTED) {
            retrying = false;
            onConnected();
          } else if (now - connectStart > ORB_STA_TIMEOUT_MS) {
            retrying = false;
            WiFi.disconnect(false, false);
            lastRetry = now;
          }
        } else if (now - lastRetry > ORB_STA_RETRY_MS && WiFi.softAPgetStationNum() == 0) {
          retrying = true;
          startSta();
        }
      }
      break;
  }
}

NetState    netState()            { return state; }
bool        netLinkUp()           { return state == NetState::Connected && WiFi.status() == WL_CONNECTED; }
const char* netIp()               { return ipStr; }
const char* netHostname()         { return host; }
const char* netSsid()             { return settings.ssid; }
int         netRssi()             { return netLinkUp() ? WiFi.RSSI() : 0; }
uint32_t    netConnectStartedMs() { return connectStart; }

bool netTimeValid() {
  return time(nullptr) > 1700000000;   // after Nov 2023 => NTP has synced
}

void netApplyTimezone() {
  setenv("TZ", settings.tz, 1);
  tzset();
}

void netRestartMdns() {
  sanitizeHostname(settings.name, host, sizeof(host));
  if (state == NetState::Connected) {
    WiFi.setHostname(host);
    startMdns();
  }
}

void netRestartIn(uint32_t ms) {
  restartAt = millis() + ms;
  if (!restartAt) restartAt = 1;
}

void netStartApScan() {
  WiFi.scanDelete();
  WiFi.scanNetworks(true, false);
}

String netScanJson() {
  String out = "[";
  int n = WiFi.scanComplete();
  int added = 0;
  for (int i = 0; i < n && added < 25; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == s) { dup = true; break; }
    if (dup) continue;
    if (added++) out += ',';
    out += '"';
    for (char c : s) {
      if (c == '"' || c == '\\') { out += '\\'; out += c; }
      else if ((uint8_t)c >= 0x20) out += c;
    }
    out += '"';
  }
  out += ']';
  return out;
}
