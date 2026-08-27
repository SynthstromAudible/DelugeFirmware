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
	// Change-detection keys: scroll_text (ui/oled/scroll_text.h) compares these against the
	// current string's pixel width and the current rect's width on every call, and re-runs the
	// legacy setup (offset 0, dir +1, the first kScrollTime pause armed) whenever either
	// differs, instead of carrying stale scroll state into now-different content. Placed last,
	// after the original three fields, so any existing positional/aggregate use of
	// {offset, dir, next_step_ms} is unaffected. Deviates from spec B section 3's fixed
	// {offset, dir, next_step_ms} layout -- see scroll_text.h and the task-4 fix-round report.
	uint16_t text_w_last = 0;
	uint16_t rect_w_last = 0;
};
struct Blink {
	uint16_t period_ms = 500;
	/// Read by the free-running blinkers (`ui/oled/values.h`'s `decimal_value`, and the `working`
	/// / status animations to come) as the phase *anchor*: the ms timestamp the current blink
	/// cycle was last restarted at, with the visible phase alternating every `period_ms` from
	/// there. Spec B section 3 declares both fields without assigning them meanings, and this is
	/// the only meaning that fits legacy's square wave (`OLED::setupBlink` / `performBlink`,
	/// hid/display/oled.cpp:832-849) into two words with no phase bit. A widget that arms a
	/// `Blink` also owns its `period_ms`, so one `Blink` serves one blinker.
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
