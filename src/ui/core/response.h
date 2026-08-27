#pragma once
#include <cstdint>

namespace deluge::ui {

struct Response {
	bool changed = false, focused = false, entered = false; // OLED widgets
	bool pressed = false, released = false, held = false;   // grid components
	uint8_t velocity = 0;
};

} // namespace deluge::ui
