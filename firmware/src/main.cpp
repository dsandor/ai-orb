// =============================================================================
//  AI Orb - live Claude Code session status on a round GC9A01 display.
//  Single cooperative loop: HTTP + DNS + Wi-Fi state machine + UI frames.
//  Everything runs on the Arduino loop task, so the session table needs no
//  locking.
// =============================================================================
#include <Arduino.h>
#include "config.h"
#include "settings.h"
#include "sessions.h"
#include "net.h"
#include "web.h"
#include "gfx.h"
#include "ui.h"
#include "demo.h"

#if PIN_BUTTON >= 0
namespace {
bool     btnDown = false;
bool     btnLongFired = false;
uint32_t btnEdge = 0;
uint32_t btnChange = 0;

void pollButton(uint32_t now) {
  bool raw = digitalRead(PIN_BUTTON) == LOW;   // active low, internal pull-up
  if (raw != btnDown) {
    if (now - btnChange < 30) return;          // debounce
    btnChange = now;
    btnDown = raw;
    if (btnDown) {
      btnEdge = now;
      btnLongFired = false;
    } else if (!btnLongFired) {
      ui::onShortPress();
    }
  } else if (btnDown && !btnLongFired && now - btnEdge >= ORB_LONG_PRESS_MS) {
    btnLongFired = true;
    ui::onLongPress();
  }
}
}  // namespace
#endif

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.printf("\n[orb] AI Orb fw %s (%s)\n", FW_VERSION, BOARD_NAME);

  settingsLoad();
  sessionsBegin();
  if (!gfx::begin(settings.screenRot, settings.brightness)) {
    Serial.println("[orb] WARNING: no frame buffer, display disabled");
  }
  Serial.printf("[orb] canvas: %s, free heap %lu, psram %lu\n", gfx::canvasInfo(),
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram());

#if PIN_BUTTON >= 0
  pinMode(PIN_BUTTON, INPUT_PULLUP);
#endif

  ui::begin();
  ui::tick(millis());   // first frame before Wi-Fi spins up
  netBegin();
  webBegin();
#if ORB_DEMO
  demoBegin();
#endif
}

void loop() {
  netLoop(millis());
  webLoop();
  // Read after webLoop: a push it just handled is stamped with millis(), and a `now` older than
  // that stamp makes every unsigned `now - stamp` wrap to ~49 days.
  const uint32_t now = millis();
  sessionsTick(now);
#if ORB_DEMO
  demoTick(now);
#endif
#if PIN_BUTTON >= 0
  pollButton(now);
#endif
  if (!ui::tick(now)) delay(1);   // yield to Wi-Fi / idle task between frames
}
