// =============================================================================
//  HTTP server
//    POST /api/state   session snapshot      (X-Orb-Token if configured)
//    POST /api/end     session ended         (X-Orb-Token if configured)
//    GET  /api/info    device info           (X-Orb-Token if configured)
//    GET  /            settings page (STA) / Wi-Fi setup portal (AP)
//    POST /settings    save settings (needs current token when one is set)
//    POST /setup       save Wi-Fi + basics from the portal, reboot
//    POST /forget-wifi, /reset (factory reset)
// =============================================================================
#include "web.h"
#include "config.h"
#include "settings.h"
#include "sessions.h"
#include "net.h"
#include "gfx.h"
#include <WebServer.h>
#include <WiFi.h>

static WebServer server(ORB_HTTP_PORT);
static uint32_t  apiCount = 0;
static uint32_t  lastApiMs = 0;

uint32_t webRequestCount() { return apiCount; }
uint32_t webLastApiMs()    { return lastApiMs; }

// -----------------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------------
static bool constEq(const char* a, const char* b) {
  size_t la = strlen(a), lb = strlen(b);
  uint8_t diff = (uint8_t)(la != lb);
  for (size_t i = 0; i < la; i++) diff |= (uint8_t)(a[i] ^ b[i % (lb ? lb : 1)]);
  return diff == 0;
}

static void sendJson(int code, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

static void sendErr(int code, const char* msg) {
  String b = "{\"ok\":false,\"error\":\"";
  b += msg;
  b += "\"}";
  sendJson(code, b);
}

// A browser marks a cross-site POST with an Origin naming the other site. Reject it, so a page
// elsewhere cannot drive the settings forms. Tools (curl, the mod) send no Origin.
static bool sameOrigin() {
  String origin = server.header("Origin");
  if (!origin.length()) return true;
  if (origin == "null") return false;   // sandboxed frames and file: pages
  int at = origin.indexOf("://");
  String host = at >= 0 ? origin.substring(at + 3) : origin;
  return host.equalsIgnoreCase(server.header("Host"));
}

// application/json cannot be sent cross-site without a CORS preflight, which this server never
// grants, so a web page cannot forge a status push as a plain form post.
static bool isJson() {
  return server.header("Content-Type").startsWith("application/json");
}

static bool apiAuthorized() {
  if (!settings.token[0]) return true;
  String t = server.header("X-Orb-Token");
  return t.length() && constEq(t.c_str(), settings.token);
}

static String esc(const char* s) {
  String o;
  for (const char* p = s ? s : ""; *p; ++p) {
    switch (*p) {
      case '&':  o += "&amp;"; break;
      case '<':  o += "&lt;"; break;
      case '>':  o += "&gt;"; break;
      case '"':  o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default:   o += *p;
    }
  }
  return o;
}

static void redirect(const char* to) {
  server.sendHeader("Location", to, true);
  server.send(303, "text/plain", "");
}

// -----------------------------------------------------------------------------
//  API
// -----------------------------------------------------------------------------
// WebServer::arg() returns a copy, so the caller must own the String for as
// long as it uses the body.
static bool readBody(String& body) {
  body = server.arg("plain");
  if (body.length() == 0) { sendErr(400, "empty body"); return false; }
  if (body.length() > ORB_MAX_BODY) { sendErr(413, "body too large"); return false; }
  return true;
}

static void handleState() {
  if (!apiAuthorized()) { sendErr(401, "unauthorized"); return; }
  if (!isJson() || !sameOrigin()) { sendErr(415, "expected application/json"); return; }
  String body;
  if (!readBody(body)) return;
  const char* err = "";
  int code = sessionsApplyState(body.c_str(), body.length(), err);
  if (code != 200) { sendErr(code, err); return; }
  apiCount++;
  lastApiMs = millis();
  sendJson(200, String("{\"ok\":true,\"sessions\":") + sessionsCount() + "}");
}

static void handleEnd() {
  if (!apiAuthorized()) { sendErr(401, "unauthorized"); return; }
  if (!isJson() || !sameOrigin()) { sendErr(415, "expected application/json"); return; }
  String body;
  if (!readBody(body)) return;
  const char* err = "";
  int code = sessionsApplyEnd(body.c_str(), body.length(), err);
  if (code != 200) { sendErr(code, err); return; }
  apiCount++;
  lastApiMs = millis();
  sendJson(200, String("{\"ok\":true,\"sessions\":") + sessionsCount() + "}");
}

static void handleInfo() {
  if (!apiAuthorized()) { sendErr(401, "unauthorized"); return; }
  String b = "{\"name\":\"";
  b += esc(netHostname());   // hostname is [a-z0-9-] so JSON-safe
  b += "\",\"fw\":\"" FW_VERSION "\",\"ip\":\"";
  b += netIp();
  b += "\",\"sessions\":";
  b += sessionsCount();
  b += ",\"uptimeMs\":";
  b += (uint32_t)millis();
  b += ",\"board\":\"" BOARD_NAME "\",\"proto\":1}";
  sendJson(200, b);
}

// -----------------------------------------------------------------------------
//  Pages
// -----------------------------------------------------------------------------
struct Tz { const char* label; const char* iana; const char* posix; };
static const Tz TZS[] = {
  {"UTC",                    "UTC",                 "UTC0"},
  {"London / Dublin",        "Europe/London",       "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Lisbon",                 "Europe/Lisbon",       "WET0WEST,M3.5.0/1,M10.5.0"},
  {"Berlin / Paris / Rome",  "Europe/Berlin",       "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Warsaw / Prague",        "Europe/Warsaw",       "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Athens / Helsinki / Kyiv","Europe/Helsinki",    "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"Istanbul",               "Europe/Istanbul",     "<+03>-3"},
  {"Moscow",                 "Europe/Moscow",       "MSK-3"},
  {"Dubai",                  "Asia/Dubai",          "<+04>-4"},
  {"India",                  "Asia/Kolkata",        "IST-5:30"},
  {"Bangkok / Jakarta",      "Asia/Bangkok",        "<+07>-7"},
  {"Singapore",              "Asia/Singapore",      "<+08>-8"},
  {"China",                  "Asia/Shanghai",       "CST-8"},
  {"Tokyo",                  "Asia/Tokyo",          "JST-9"},
  {"Seoul",                  "Asia/Seoul",          "KST-9"},
  {"Sydney / Melbourne",     "Australia/Sydney",    "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Brisbane",               "Australia/Brisbane",  "AEST-10"},
  {"Auckland",               "Pacific/Auckland",    "NZST-12NZDT,M9.5.0,M4.1.0/3"},
  {"Sao Paulo",              "America/Sao_Paulo",   "<-03>3"},
  {"New York (Eastern)",     "America/New_York",    "EST5EDT,M3.2.0,M11.1.0"},
  {"Chicago (Central)",      "America/Chicago",     "CST6CDT,M3.2.0,M11.1.0"},
  {"Denver (Mountain)",      "America/Denver",      "MST7MDT,M3.2.0,M11.1.0"},
  {"Phoenix",                "America/Phoenix",     "MST7"},
  {"Los Angeles (Pacific)",  "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
  {"Anchorage",              "America/Anchorage",   "AKST9AKDT,M3.2.0,M11.1.0"},
  {"Honolulu",               "Pacific/Honolulu",    "HST10"},
};

static const char CSS[] PROGMEM = R"CSS(
:root{--bg:#0d0c0e;--card:#18161a;--line:#2a272d;--text:#f4efea;--dim:#9a938c;--acc:#d97757}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);
font:15px/1.45 -apple-system,BlinkMacSystemFont,"Inter","Segoe UI",sans-serif}
main{max-width:520px;margin:0 auto;padding:22px 16px 48px}
h1{font-size:22px;margin:0 0 2px;display:flex;align-items:center;gap:10px}
h1 i{width:14px;height:14px;border-radius:50%;background:var(--acc);box-shadow:0 0 14px var(--acc)}
h2{font-size:13px;letter-spacing:.08em;text-transform:uppercase;color:var(--dim);margin:26px 0 8px}
.sub{color:var(--dim);font-size:13px}.card{background:var(--card);border:1px solid var(--line);
border-radius:14px;padding:14px 16px}label{display:block;margin:12px 0 4px;color:var(--dim);font-size:13px}
input,select{width:100%;padding:10px 12px;border-radius:10px;border:1px solid var(--line);
background:#0f0e10;color:var(--text);font-size:15px}input[type=checkbox]{width:auto;margin-right:8px}
.row{display:flex;gap:10px}.row>*{flex:1}button{margin-top:16px;width:100%;padding:12px;border:0;
border-radius:10px;background:var(--acc);color:#1a0f0b;font-weight:700;font-size:15px}
button.ghost{background:transparent;color:var(--text);border:1px solid var(--line)}
button.danger{background:transparent;color:#f0574f;border:1px solid #5a2725}
table{width:100%;border-collapse:collapse;font-size:13px}td{padding:6px 4px;border-bottom:1px solid var(--line)}
td:last-child{text-align:right;color:var(--dim)}.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;font-size:13px}
.kv b{color:var(--dim);font-weight:500}.ok{color:#5ed38c}.note{background:#1d2a20;border:1px solid #2f4a36;
padding:10px 12px;border-radius:10px;margin:14px 0;font-size:14px}code{color:var(--acc)}
)CSS";

static void pageHead(String& h, const char* title) {
  h += F("<!doctype html><html><head><meta charset=utf-8>"
         "<meta name=viewport content='width=device-width,initial-scale=1'><title>");
  h += title;
  h += F("</title><style>");
  h += FPSTR(CSS);
  h += F("</style></head><body><main>");
}

static void tzOptions(String& h, bool autoSelect) {
  bool matched = false;
  for (const Tz& t : TZS) {
    bool sel = !strcmp(t.posix, settings.tz) && !matched;
    if (sel) matched = true;
    h += "<option data-iana='"; h += t.iana; h += "' value='"; h += esc(t.posix); h += "'";
    if (sel) h += " selected";
    h += ">"; h += t.label; h += "</option>";
  }
  h += "<option value='__custom'";
  if (!matched) h += " selected";
  h += ">Custom POSIX TZ...</option>";
  (void)autoSelect;
}

static const char TZ_JS[] PROGMEM = R"JS(
<script>
(function(){var s=document.getElementById('tzsel'),c=document.getElementById('tzc');
function u(){c.parentNode.style.display=s.value=='__custom'?'block':'none'}
s.onchange=u;if(s.dataset.auto=='1'){try{var z=Intl.DateTimeFormat().resolvedOptions().timeZone;
for(var i=0;i<s.options.length;i++)if(s.options[i].dataset.iana==z){s.selectedIndex=i;break}}catch(e){}}u();})();
</script>)JS";

static const char* resolveTz() {
  static char buf[64];
  String sel = server.arg("tz");
  if (sel == "__custom") sel = server.arg("tzc");
  sel.trim();
  if (sel.length() == 0 || sel.length() >= sizeof(buf)) return settings.tz;
  strlcpy(buf, sel.c_str(), sizeof(buf));
  return buf;
}

static bool formAuthorized();

static void handleSetupPage() {
  String h;
  h.reserve(9000);
  pageHead(h, "Claude Orb setup");
  h += F("<h1><i></i>Claude Orb</h1><div class=sub>Wi-Fi setup</div>"
         "<form method=post action=/setup><h2>Network</h2><div class=card>"
         "<label>Wi-Fi network</label><input name=ssid list=nets required maxlength=32 value='");
  h += esc(settings.ssid);
  h += F("'><datalist id=nets></datalist><label>Password</label>"
         "<input name=pass type=password maxlength=64 placeholder='leave empty for open networks'>"
         "</div><h2>Device</h2><div class=card><label>Name (mDNS host)</label><input name=name maxlength=31 value='");
  h += esc(settings.name);
  h += F("'><label>API token (optional, sent as X-Orb-Token)</label>"
         "<input name=token type=password maxlength=64 autocomplete=off placeholder='");
  h += settings.token[0] ? F("set: leave empty to keep it") : F("none");
  h += F("'>");
  if (settings.token[0]) h += F("<label>Current token (required to change it)</label><input name=cur type=password autocomplete=off>");
  h += F("<label>Time zone</label><select name=tz id=tzsel data-auto=1>");
  tzOptions(h, true);
  h += F("</select><div style='display:none'><label>POSIX TZ string</label><input name=tzc id=tzc value='");
  h += esc(settings.tz);
  h += F("'></div></div><button>Save &amp; connect</button></form>"
         "<p class=sub>The orb reboots and shows its IP address once it joins your network.</p>");
  h += FPSTR(TZ_JS);
  h += F("<script>fetch('/scan').then(r=>r.json()).then(l=>{var d=document.getElementById('nets');"
         "l.forEach(s=>{var o=document.createElement('option');o.value=s;d.appendChild(o)})}).catch(()=>{});</script>"
         "</main></body></html>");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", h);
}

static void handleSetupSave() {
  // Outside setup mode the Wi-Fi is changed from the settings page, which asks for the token.
  if (netState() != NetState::Setup || !sameOrigin()) { server.send(403, "text/plain", "Forbidden"); return; }
  String tok = server.arg("token"); tok.trim();
  if (tok.length() && !formAuthorized()) { server.send(403, "text/plain", "Current token required to change it"); return; }
  String ssid = server.arg("ssid");
  ssid.trim();
  if (!ssid.length() || ssid.length() > 32) { server.send(400, "text/plain", "SSID required"); return; }
  strlcpy(settings.ssid, ssid.c_str(), sizeof(settings.ssid));
  strlcpy(settings.pass, server.arg("pass").c_str(), sizeof(settings.pass));
  String name = server.arg("name"); name.trim();
  if (name.length()) strlcpy(settings.name, name.c_str(), sizeof(settings.name));
  if (tok.length()) strlcpy(settings.token, tok.c_str(), sizeof(settings.token));
  strlcpy(settings.tz, resolveTz(), sizeof(settings.tz));
  settingsSave();
  String h;
  pageHead(h, "Saved");
  h += F("<h1><i></i>Saved</h1><div class=note>Rebooting and joining <b>");
  h += esc(settings.ssid);
  h += F("</b>. Reconnect your phone to your normal Wi-Fi; the orb shows its IP when it is online "
         "(also <code>http://");
  char hostBuf[40];
  sanitizeHostname(settings.name, hostBuf, sizeof(hostBuf));
  h += hostBuf;
  h += F(".local</code>).</div></main></body></html>");
  server.send(200, "text/html", h);
  netRestartIn(1500);
}

static String fmtAge(uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60) return String(s) + "s";
  if (s < 3600) return String(s / 60) + "m";
  return String(s / 3600) + "h";
}

static void handleSettingsPage() {
  String h;
  h.reserve(11000);
  pageHead(h, "Claude Orb");
  h += F("<h1><i></i>Claude Orb</h1><div class=sub>");
  h += esc(netHostname());
  h += F(".local &middot; ");
  h += netIp();
  h += F(" &middot; fw " FW_VERSION "</div>");
  if (server.hasArg("saved")) h += F("<div class=note>Settings saved.</div>");
  if (server.hasArg("denied")) h += F("<div class=note style='background:#2a1716;border-color:#5a2725'>Wrong token - nothing changed.</div>");

  // Status
  h += F("<h2>Status</h2><div class=card><div class=kv>");
  h += F("<b>Wi-Fi</b><span>"); h += esc(netSsid()); h += " ("; h += netRssi(); h += F(" dBm)</span>");
  uint32_t up = millis() / 1000;
  h += F("<b>Uptime</b><span>"); h += up / 3600; h += "h "; h += (up / 60) % 60; h += F("m</span>");
  h += F("<b>API requests</b><span>"); h += apiCount; h += F("</span>");
  h += F("<b>Auth</b><span>"); h += settings.token[0] ? F("token required") : F("open (no token)"); h += F("</span>");
  h += F("<b>Canvas</b><span>"); h += gfx::canvasInfo(); h += F("</span>");
  h += F("<b>Free heap</b><span>"); h += ESP.getFreeHeap() / 1024; h += F(" KB</span>");
  h += F("</div></div>");

  h += F("<h2>Sessions</h2><div class=card>");
  if (!sessionsCount()) {
    h += F("<span class=sub>No sessions. Point the Claude Code mod at <code>http://");
    h += esc(netHostname());
    h += F(".local/api/state</code>.</span>");
  } else {
    h += F("<table>");
    uint32_t now = millis();
    for (uint8_t i = 0; i < ORB_MAX_SESSIONS; i++) {
      Session* s = sessionAt(i);
      if (!s) continue;
      h += "<tr><td>"; h += esc(s->project); h += " <span class=sub>"; h += esc(s->sid);
      h += "</span></td><td>"; h += sstateName(s->state); h += "</td><td>";
      h += fmtAge(now - s->rxMs); h += sessionIsStale(*s, now) ? " (stale)" : ""; h += "</td></tr>";
    }
    h += F("</table>");
  }
  h += F("</div>");

  // Settings form
  h += F("<form method=post action=/settings><h2>Settings</h2><div class=card>");
  if (settings.token[0]) {
    h += F("<label>Current token (required to save)</label><input name=cur type=password autocomplete=off required>");
  }
  h += F("<label>Name (mDNS host)</label><input name=name maxlength=31 value='");
  h += esc(settings.name);
  h += F("'><label>New API token (blank = keep")
       ;
  h += settings.token[0] ? F(", tick below to remove)") : F(")");
  h += F("</label><input name=token maxlength=64 autocomplete=off>");
  if (settings.token[0]) h += F("<label><input type=checkbox name=cleartoken value=1>Remove token (open API)</label>");
  h += F("<label>Time zone</label><select name=tz id=tzsel data-auto=0>");
  tzOptions(h, false);
  h += F("</select><div style='display:none'><label>POSIX TZ string</label><input name=tzc id=tzc value='");
  h += esc(settings.tz);
  h += F("'></div><div class=row><div><label>Page rotation (s)</label><input name=rot type=number min=3 max=60 value='");
  h += settings.rotateSec;
  h += F("'></div><div><label>Screen rotation</label><select name=srot>");
  for (int r = 0; r < 4; r++) {
    h += "<option value="; h += r; if (r == settings.screenRot) h += " selected";
    h += ">"; h += r * 90; h += "&deg;</option>";
  }
  h += F("</select></div></div>");
#if PIN_BL >= 0
  h += F("<label>Brightness</label><input name=bri type=range min=10 max=255 value='");
  h += settings.brightness;
  h += F("'>");
#endif
  h += F("<label><input type=checkbox name=h24 value=1");
  if (settings.clock24h) h += F(" checked");
  h += F(">24-hour clock</label></div><button>Save</button></form>");

  // Danger zone
  h += F("<h2>Maintenance</h2><div class=card>"
         "<form method=post action=/forget-wifi onsubmit=\"return confirm('Forget Wi-Fi and reboot into setup mode?')\">");
  if (settings.token[0]) h += F("<input name=cur type=password placeholder='current token' required>");
  h += F("<button class=ghost>Forget Wi-Fi</button></form>"
         "<form method=post action=/reset onsubmit=\"return confirm('Erase ALL settings and reboot?')\">");
  if (settings.token[0]) h += F("<input name=cur type=password placeholder='current token' required style='margin-top:12px'>");
  h += F("<button class=danger>Factory reset</button></form></div>");
  h += FPSTR(TZ_JS);
  h += F("</main></body></html>");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", h);
}

static bool formAuthorized() {
  if (!sameOrigin()) return false;
  if (!settings.token[0]) return true;
  return constEq(server.arg("cur").c_str(), settings.token);
}

static void handleSettingsSave() {
  if (!formAuthorized()) { redirect("/?denied=1"); return; }
  String name = server.arg("name"); name.trim();
  bool nameChanged = false;
  if (name.length() && name != settings.name) {
    strlcpy(settings.name, name.c_str(), sizeof(settings.name));
    nameChanged = true;
  }
  if (server.arg("cleartoken") == "1") {
    settings.token[0] = '\0';
  } else {
    String tok = server.arg("token"); tok.trim();
    if (tok.length()) strlcpy(settings.token, tok.c_str(), sizeof(settings.token));
  }
  strlcpy(settings.tz, resolveTz(), sizeof(settings.tz));
  if (server.hasArg("rot")) settings.rotateSec = constrain(server.arg("rot").toInt(), 3, 60);
  if (server.hasArg("srot")) settings.screenRot = server.arg("srot").toInt() & 3;
  if (server.hasArg("bri")) settings.brightness = constrain(server.arg("bri").toInt(), 10, 255);
  settings.clock24h = server.arg("h24") == "1";
  settingsSave();

  netApplyTimezone();
  gfx::setRotation(settings.screenRot);
  gfx::setBrightness(settings.brightness);
  if (nameChanged) netRestartMdns();
  redirect("/?saved=1");
}

static void handleForgetWifi() {
  if (!formAuthorized()) { redirect("/?denied=1"); return; }
  settings.ssid[0] = settings.pass[0] = '\0';
  settingsSave();
  server.send(200, "text/html",
              "<meta name=viewport content='width=device-width'><body style='background:#0d0c0e;color:#f4efea;"
              "font-family:sans-serif;padding:24px'>Wi-Fi forgotten. Rebooting into setup mode: join "
              "<b>" ORB_AP_SSID "</b>.</body>");
  netRestartIn(1500);
}

static void handleReset() {
  if (!formAuthorized()) { redirect("/?denied=1"); return; }
  settingsFactoryReset();
  server.send(200, "text/html",
              "<meta name=viewport content='width=device-width'><body style='background:#0d0c0e;color:#f4efea;"
              "font-family:sans-serif;padding:24px'>Factory reset done. Rebooting into setup mode: join "
              "<b>" ORB_AP_SSID "</b>.</body>");
  netRestartIn(1500);
}

static void handleRoot() {
  if (netState() == NetState::Setup) handleSetupPage();
  else handleSettingsPage();
}

static void captiveRedirect() {
  String url = "http://";
  url += netIp();
  url += "/";
  server.sendHeader("Location", url, true);
  server.send(302, "text/plain", "");
}

static void handleNotFound() {
  if (netState() == NetState::Setup) { captiveRedirect(); return; }
  sendErr(404, "not found");
}

void webBegin() {
  static const char* hdrs[] = {"X-Orb-Token", "Origin", "Host", "Content-Type"};
  server.collectHeaders(hdrs, 4);

  server.on("/api/state", HTTP_POST, handleState);
  server.on("/api/end",   HTTP_POST, handleEnd);
  server.on("/api/info",  HTTP_GET,  handleInfo);
  server.on("/",          HTTP_GET,  handleRoot);
  server.on("/settings",  HTTP_POST, handleSettingsSave);
  server.on("/setup",     HTTP_POST, handleSetupSave);
  server.on("/forget-wifi", HTTP_POST, handleForgetWifi);
  server.on("/reset",     HTTP_POST, handleReset);
  server.on("/scan",      HTTP_GET,  []() {
    String j = netScanJson();
    if (netState() == NetState::Setup && WiFi.scanComplete() < 0 && WiFi.scanComplete() != WIFI_SCAN_RUNNING)
      netStartApScan();
    sendJson(200, j);
  });
  // Captive-portal probes (Android, Apple, Windows) -> portal.
  for (const char* p : {"/generate_204", "/gen_204", "/hotspot-detect.html",
                        "/connecttest.txt", "/ncsi.txt", "/fwlink", "/redirect"}) {
    server.on(p, HTTP_ANY, []() {
      if (netState() == NetState::Setup) captiveRedirect();
      else server.send(204, "text/plain", "");
    });
  }
  server.onNotFound(handleNotFound);
  server.begin();
}

void webLoop() { server.handleClient(); }
