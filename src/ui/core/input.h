#pragma once
#include <cstdint>
#include <span>

namespace deluge::ui {

enum class EncoderRole : uint8_t { Select, Horizontal, Vertical, GoldUpper, GoldLower, Tempo, Count };

struct PadEvent {
	uint8_t x, y;
	uint8_t velocity;
	bool pressed;
}; // velocity 0 = release

/// Input as the toolkit sees it for one tick. Lowered by the shell; the only input a widget can read.
struct InputSnapshot {
	int8_t turn;    // Select encoder detents: navigate / move focus
	int8_t edit[2]; // GoldUpper, GoldLower detents: edit the focused value
	int8_t horizontal, vertical;
	bool select_pressed, back_pressed, shift;
	std::span<const PadEvent> pads; // this tick's pad events; grid frames filter by region
	uint32_t now_ms;
};

} // namespace deluge::ui
