// Desktop preview harness - see build.zsh. Stubs the network layer and feeds
// sessions through the real JSON parser, then renders each screen.
#include <Arduino.h>
#include "gfx.h"
#include "ui_common.h"
#include "settings.h"
#include "net.h"
#include "web.h"
#include <time.h>
#include <vector>
#include <string>
uint32_t g_millis = 100000;
EspShim ESP; SerialShim Serial;
OrbSettings settings;
// --- stubs ---
NetState netState() { return NetState::Connected; }
bool netLinkUp() { return true; }
const char* netIp() { return "192.168.1.42"; }
const char* netHostname() { return "claude-orb"; }
const char* netSsid() { return "HomeNet"; }
int netRssi() { return -52; }
bool netTimeValid() { return true; }
uint32_t webRequestCount() { return 1234; }
bool settingsHasWifi() { return true; }

static std::vector<uint16_t> frames;
static void grab() {
  auto& c = gfx::canvas();
  for (int y = 0; y < 240; y++) for (int x = 0; x < 240; x++) {
    uint16_t v = c.readPixel(x, y);
    float dx = x - 119.5f, dy = y - 119.5f;
    if (dx*dx + dy*dy > 120.5f*120.5f) v = 0x2104;  // outside the round glass
    frames.push_back(v);
  }
}
static void post(const std::string& j) { const char* e; if (sessionsApplyState(j.data(), j.size(), e) != 200) fprintf(stderr, "post failed %s\n", e); }
static ui::Frame F(int slot) { return ui::Frame{g_millis, (int8_t)slot, 0, 3, false}; }

int main() {
  memset(&settings, 0, sizeof settings);
  strcpy(settings.name, "claude-orb"); strcpy(settings.tz, "UTC0"); settings.clock24h = true; settings.rotateSec = 8;
  gfx::begin(0, 255);
  sessionsBegin();
  post(R"({"v":1,"sid":"8f2c1a","project":"aistatus","state":"tool","tool":"Bash","detail":"npm test -- --watch=false --reporter=dot","model":"Opus 5.5","effort":"high","mode":"plan",
  "ctx":{"pct":62.5,"tokens":125000,"window":200000,"compactions":2},"cost":1.234,
  "limits":[{"kind":"5h","pct":34,"resetInMs":5400000},{"kind":"7d","pct":81,"resetInMs":302400000}],
  "turn":{"running":true,"elapsedMs":41000,"steps":7,"edits":2,"errors":1},
  "last":{"durationMs":63000,"costDelta":0.21,"edits":4,"errors":0},
  "agents":{"running":2,"total":3,"list":[{"name":"Explore","task":"find hook registration points","status":"running","ctx":12000,"steps":4,"elapsedMs":30000},
     {"name":"code-reviewer","task":"review core.ts","status":"running","ctx":8000,"steps":9,"elapsedMs":95000},
     {"name":"Plan","task":"design protocol","status":"done","ctx":20000,"steps":12,"elapsedMs":61000}]},
  "gate":{"allowed":31,"asked":3,"denied":1,"pending":0},"ttftMs":820,"log":["edit core.ts","bash: npm test","spawn Explore"]})");
  post(R"({"sid":"b2","project":"website","state":"thinking","model":"Sonnet 5","ctx":{"pct":18},"cost":0.4,"turn":{"running":true,"elapsedMs":12000,"steps":2}})");
  post(R"({"sid":"c3","project":"infra-terraform","state":"waiting","tool":"Bash","detail":"terraform apply -auto-approve","ctx":{"pct":88},"turn":{"running":true,"elapsedMs":200000},"gate":{"pending":1}})");
  int a = sessionFind("8f2c1a"), b = sessionFind("b2"), c = sessionFind("c3");
  std::vector<uint32_t> times = {0, 333};
  auto shot = [&](auto fn) { for (int i = 0; i < 45; i++) { g_millis += 33; gfx::clear(); fn(); } grab(); };
  g_millis += 1234;
  shot([&]{ ui::drawSession(*sessionAt(a), F(a)); });
  shot([&]{ ui::drawSession(*sessionAt(b), F(b)); });
  shot([&]{ ui::drawAgents(*sessionAt(a), F(a)); });
  shot([&]{ ui::drawLimits(*sessionAt(a), F(a)); });
  g_millis += 300;
  shot([&]{ ui::drawAttention(*sessionAt(c), F(c)); });
  shot([&]{ ui::drawDone(*sessionAt(a), F(a), g_millis - 700, ORB_DONE_FLASH_MS); });
  shot([&]{ ui::drawIdle(F(-1)); });
  shot([&]{ ui::drawSetup(F(-1)); });
  shot([&]{ ui::drawConnecting(F(-1)); });
  shot([&]{ ui::drawConnected(F(-1), g_millis - 2000); });
  shot([&]{ ui::drawInfo(F(-1)); });
  // stale + idle session
  post(R"({"sid":"b2","project":"website","state":"idle","model":"Sonnet 5","ctx":{"pct":18},"cost":0.4,"turn":{"running":false},"last":{"durationMs":75000,"costDelta":0.12,"edits":1,"errors":0},"log":["edit index.html"]})");
  g_millis += 70000;
  shot([&]{ ui::drawSession(*sessionAt(b), F(b)); });
  // extra: high ctx + long names + error state, compacting, multi waiting dots
  post(R"({"sid":"8f2c1a","project":"a-really-long-project-name-here","state":"error","tool":"Edit","detail":"src/components/Sidebar.tsx","model":"Opus 5.5","effort":"max","mode":"bypassPermissions","ctx":{"pct":93,"tokens":186000,"compactions":7},"cost":12.5,"limits":[{"kind":"5h","pct":95,"resetInMs":600000}],"turn":{"running":true,"elapsedMs":3725000,"steps":120,"edits":31,"errors":3}})");
  shot([&]{ ui::drawSession(*sessionAt(a), F(a)); });
  post(R"({"sid":"b2","project":"website","state":"compacting","ctx":{"pct":97},"turn":{"running":true,"elapsedMs":5000}})");
  shot([&]{ ui::drawSession(*sessionAt(b), F(b)); });
  shot([&]{ ui::drawAgents(*sessionAt(a), F(a)); });
  // TIMING: render the busiest screens repeatedly (host time, for relative cost)
  {
    auto t0 = clock();
    for (int i = 0; i < 300; i++) { g_millis += 33; gfx::clear(); ui::drawSession(*sessionAt(a), F(a)); }
    auto t1 = clock();
    for (int i = 0; i < 300; i++) { g_millis += 33; gfx::clear(); ui::drawAttention(*sessionAt(c), F(c)); }
    auto t2 = clock();
    for (int i = 0; i < 300; i++) { g_millis += 33; gfx::clear(); ui::drawIdle(F(-1)); }
    auto t3 = clock();
    printf("host us/frame: session %.0f attention %.0f idle %.0f\n", (t1-t0)*1e6/CLOCKS_PER_SEC/300, (t2-t1)*1e6/CLOCKS_PER_SEC/300, (t3-t2)*1e6/CLOCKS_PER_SEC/300);
  }
  // contact sheet 4 x 4
  int n = frames.size() / (240*240), cols = 4, rows = (n + cols - 1) / cols, G = 8;
  int W = cols*240 + (cols+1)*G, H = rows*240 + (rows+1)*G;
  FILE* f = fopen("sheet.ppm", "wb"); fprintf(f, "P6 %d %d 255\n", W, H);
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
    int cx = (x - G) / (240 + G), cy = (y - G) / (240 + G);
    int px = (x - G) % (240 + G), py = (y - G) % (240 + G);
    uint16_t v = 0x4208;
    int idx = cy*cols + cx;
    if (x >= G && y >= G && px < 240 && py < 240 && idx < n) v = frames[idx*57600 + py*240 + px];
    unsigned char rgb[3] = {(unsigned char)(((v>>11)&31)*255/31), (unsigned char)(((v>>5)&63)*255/63), (unsigned char)((v&31)*255/31)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  printf("%d frames\n", n);
}
