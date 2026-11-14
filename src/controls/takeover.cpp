/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "controls/takeover.h"

#include <algorithm>
#include <cmath>
#include <utility>

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

std::vector<TakeoverStore::Held>::iterator TakeoverStore::locate(const project::OwnerParamTarget& target,
                                                                 const project::MidiCommand& source) {
	return std::ranges::find_if(states_, [&](const Held& h) { return h.target == target && h.source == source; });
}

TakeoverState& TakeoverStore::at(const project::OwnerParamTarget& target, const project::MidiCommand& source) {
	auto held = locate(target, source);
	if (held == states_.end()) {
		states_.push_back(Held{.target = target, .source = source, .state = TakeoverState{}});
		return states_.back().state;
	}
	return held->state;
}

const TakeoverState* TakeoverStore::find(const project::OwnerParamTarget& target,
                                         const project::MidiCommand& source) const {
	const auto held =
	    std::ranges::find_if(states_, [&](const Held& h) { return h.target == target && h.source == source; });
	return held != states_.end() ? &held->state : nullptr;
}

void TakeoverStore::relearn(const project::OwnerParamTarget& target, const project::MidiCommand& from,
                            const project::MidiCommand& to) {
	if (from == to) {
		return;
	}
	const auto old = locate(target, from);
	if (old == states_.end()) {
		forget(target, to);
		return;
	}
	const TakeoverState state = old->state;
	forget(target, from);
	at(target, to) = state;
}

void TakeoverStore::clone_owner(project::ControlsAddress from, project::ControlsAddress to) {
	if (from == to) {
		return;
	}
	forget_owner(to);
	const size_t held = states_.size();
	for (size_t i = 0; i < held; i++) {
		if (states_[i].target.owner == from) {
			Held copy = states_[i];
			copy.target.owner = to;
			states_.push_back(std::move(copy));
		}
	}
}

void TakeoverStore::forget_owner(project::ControlsAddress owner) {
	std::erase_if(states_, [&](const Held& h) { return h.target.owner == owner; });
}

void TakeoverStore::forget(const project::OwnerParamTarget& target, const project::MidiCommand& source) {
	std::erase_if(states_, [&](const Held& h) { return h.target == target && h.source == source; });
}

void TakeoverStore::clear() {
	states_.clear();
}

} // namespace deluge::controls
