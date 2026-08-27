#pragma once
#include <cstdint>

namespace deluge::ui {

// POD state the scene owns and passes by reference.
struct ListState {
	int16_t cursor = 0, scroll = 0, rows_last = 0;
	bool editing = false;
};
struct ColumnsState {
	int8_t focused = 0;
	uint8_t page = 0;
};
struct ScrollText {
	int16_t offset = 0;
	int8_t dir = 1;
	uint32_t next_step_ms = 0;
};
struct Blink {
	uint16_t period_ms = 500;
	uint32_t until_ms = 0;
};
struct Hold {
	uint32_t down_ms = 0;
	bool held = false;
	uint8_t x = 0, y = 0;
};
struct Timer {
	uint32_t due_ms = 0;
	bool armed = false;
};
struct WaveformState {
	int32_t scroll_cols = 0;
	uint8_t zoom = 0;
};

} // namespace deluge::ui
