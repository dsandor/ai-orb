#pragma once
// Screen manager: picks the view (focus/attention/idle/...), rotates pages,
// runs transitions and renders one frame into the canvas.
#include <stdint.h>

namespace ui {
enum class Page : uint8_t { Session, Agents, Limits };

void     begin();
// Renders + presents a frame if one is due. Returns true if it drew.
bool     tick(uint32_t now);
void     onShortPress();     // next page / next session
void     onLongPress();      // device info overlay (toggle)
// Shows one page of a session for holdMs, like a button press would (demo mode).
void     pinPage(int8_t slot, Page page, uint32_t holdMs);
}  // namespace ui
