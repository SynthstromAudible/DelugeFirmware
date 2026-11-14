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

#include "controls/knob_math.h"

#include <algorithm>
#include <limits>

namespace deluge::controls {

namespace {

constexpr int32_t kTop = std::numeric_limits<int32_t>::max();
constexpr int32_t kMaxKnobPos = 64;
/// The knob's whole span, -64..64, as one 0..128 range.
constexpr int32_t kKnobSpan = 128;
/// The expression dimension that is bipolar and scales as a whole word: pitch bend.
constexpr uint32_t kPitchBend = 0;

bool cable(const project::ParamAddress& address) {
	return address.space == project::ParamSpace::ENGINE && (address.id.value & plugin::kCableDepthFlag) != 0;
}

/// A word that is never negative (0..INT32_MAX) onto -64..64: a half-precision param or an expression dimension past
/// bend.
bool half_word(const project::ParamAddress& address, const plugin::ParamInfo* info) {
	if (address.space == project::ParamSpace::EXPRESSION) {
		return address.id.value != kPitchBend;
	}
	return info != nullptr && info->mapping == plugin::MappingKind::UNIPOLAR;
}

} // namespace

KnobKind knob_kind(const project::ParamAddress& address) {
	if (cable(address)) {
		return KnobKind::CABLE;
	}
	return address.space == project::ParamSpace::MIDI_CC ? KnobKind::MIDI_CC : KnobKind::PARAM;
}

int32_t knob_pos_of(const project::ParamAddress& address, const plugin::ParamInfo* info, int32_t value) {
	if (cable(address)) {
		return (value >> 23) - 64;
	}
	if (half_word(address, info)) {
		return (value >> 24) - 64;
	}
	if (value >= static_cast<int32_t>(0x80000000u - (1u << 24))) {
		return kMaxKnobPos;
	}
	return (value + (1 << 24)) >> 25;
}

int32_t value_of_knob(const project::ParamAddress& address, const plugin::ParamInfo* info, int32_t knob_pos) {
	if (cable(address)) {
		return knob_pos < kMaxKnobPos ? (knob_pos + 64) << 23 : 1 << 30;
	}
	if (half_word(address, info)) {
		return knob_pos < kMaxKnobPos ? (knob_pos + 64) << 24 : kTop;
	}
	return knob_pos < kMaxKnobPos ? knob_pos << 25 : kTop;
}

bool indicates_middle(const project::ParamAddress& address, const plugin::ParamInfo* info, bool stutter_quantized,
                      bool stuttering) {
	if (cable(address)) {
		return true;
	}
	if (info == nullptr) {
		return false;
	}
	if (info->file_name == "stutterRate") {
		return !stutter_quantized || stuttering;
	}
	return info->flags.has(plugin::ParamFlag::MIDDLE_DETENT);
}

bool bipolar(const project::ParamAddress& address, const plugin::ParamInfo* info) {
	if (cable(address)) {
		return true;
	}
	if (address.space == project::ParamSpace::EXPRESSION) {
		return address.id.value == kPitchBend;
	}
	return info != nullptr && info->flags.has(plugin::ParamFlag::CENTRED);
}

int32_t turn(KnobKind kind, int32_t knob_pos, int32_t offset) {
	const int32_t lower = std::min(kind == KnobKind::CABLE ? -192 : -64, knob_pos);
	const int32_t upper = kind == KnobKind::MIDI_CC ? 63 : 64;
	return std::clamp(knob_pos + offset, lower, upper);
}

int32_t fold_cable(int32_t knob_pos) {
	// Centred on 64; without the nudge, a position of 0 and one just past it would both map to 64 and blink twice.
	int32_t folded = (knob_pos + kKnobSpan) >> 1;
	if (folded == kMaxKnobPos && knob_pos != 0) {
		folded += knob_pos;
	}
	return folded;
}

} // namespace deluge::controls
