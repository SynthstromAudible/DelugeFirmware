#include "controls/takeover.h"

#include <algorithm>
#include <cmath>

namespace deluge::controls {

namespace {

constexpr int32_t kMaxValue = 127;
constexpr int32_t kKnobMax = 64;
constexpr int32_t kKnobMin = -64;

/// SCALE's step: the knob moves along its runway by the share of the controller's runway the controller just covered.
int32_t scaled(int32_t knob_pos, int32_t midi_pos, int32_t previous) {
	const int32_t change = midi_pos - previous;
	if (change == 0) {
		return knob_pos;
	}
	auto result = static_cast<float>(knob_pos);
	const auto change_float = static_cast<float>(change);
	if (change > 0) {
		const int32_t midi_runway = kKnobMax - midi_pos;
		if (midi_runway != 0) {
			result =
			    result + (static_cast<float>(kKnobMax - knob_pos) * (change_float / static_cast<float>(midi_runway)));
			// close to each other, the share can round below where the knob is
			result = std::max(result, static_cast<float>(knob_pos));
		}
	}
	else {
		const int32_t midi_runway = midi_pos - kKnobMin;
		if (midi_runway != 0) {
			result =
			    result + (static_cast<float>(knob_pos - kKnobMin) * (change_float / static_cast<float>(midi_runway)));
			result = std::min(result, static_cast<float>(knob_pos));
		}
	}
	return std::clamp(static_cast<int32_t>(std::round(result)), kKnobMin, kKnobMax);
}

} // namespace

int32_t knob_pos_after(int32_t knob_pos, uint8_t value, TakeoverInputs inputs, TakeoverState& state) {
	const int32_t midi_pos = value < kMaxValue ? value - 64 : kKnobMax;

	if (inputs.cable_relative || inputs.knob_relative || inputs.mode == TakeoverMode::RELATIVE) {
		int32_t offset = value;
		if (offset >= 64) {
			offset -= 128;
		}
		const int32_t lower = std::min(kKnobMin, knob_pos);
		const int32_t moved = std::min(std::max(knob_pos + offset, lower), kKnobMax);
		state.previous = moved;
		return moved;
	}
	if (inputs.mode == TakeoverMode::JUMP || inputs.recording || inputs.step_editing) {
		state.previous = midi_pos;
		return midi_pos;
	}

	const int32_t previous = state.previous.value_or(midi_pos);
	int32_t result = knob_pos;
	if ((previous <= knob_pos && midi_pos >= knob_pos) || (previous >= knob_pos && midi_pos <= knob_pos)) {
		result = midi_pos;
	}
	else if (inputs.mode == TakeoverMode::SCALE) {
		result = scaled(knob_pos, midi_pos, previous);
	}
	state.previous = midi_pos;
	return result;
}

} // namespace deluge::controls
