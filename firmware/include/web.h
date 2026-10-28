#pragma once
// HTTP server on port 80: protocol API (docs/protocol.md), settings page,
// and the captive setup portal while in AP mode.
#include <stdint.h>

void     webBegin();
void     webLoop();
uint32_t webRequestCount();      // API requests served (for the info overlay)
uint32_t webLastApiMs();         // millis() of the last accepted API request
