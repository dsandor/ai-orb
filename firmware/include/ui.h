#pragma once
// Screen manager: picks the view (focus/attention/idle/...), rotates pages,
// runs transitions and renders one frame into the canvas.
#include <stdint.h>

namespace ui {
void     begin();
// Renders + presents a frame if one is due. Returns true if it drew.
bool     tick(uint32_t now);
void     onShortPress();     // next page / next session
void     onLongPress();      // device info overlay (toggle)
}  // namespace ui
