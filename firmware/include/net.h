#pragma once
// Wi-Fi (STA with AP-portal fallback), mDNS, NTP.
#include <Arduino.h>

enum class NetState : uint8_t {
  Connecting,   // STA attempt in progress (boot, or retry from AP mode)
  Connected,    // STA up, HTTP API reachable on the LAN
  Setup,        // captive-portal AP "Claude-Orb-Setup" is up
};

void        netBegin();
void        netLoop(uint32_t now);
NetState    netState();
bool        netLinkUp();              // STA currently associated + has IP
const char* netIp();                  // STA IP, or AP IP in setup mode
const char* netHostname();            // sanitized mDNS host (no ".local")
const char* netSsid();                // configured SSID
int         netRssi();
uint32_t    netConnectStartedMs();
bool        netTimeValid();           // NTP has produced a plausible time
void        netApplyTimezone();       // re-reads settings.tz
void        netRestartMdns();         // after a name change
void        netRestartIn(uint32_t ms);
void        netStartApScan();         // async scan for the setup page
String      netScanJson();            // ["ssid", ...] of the last scan
