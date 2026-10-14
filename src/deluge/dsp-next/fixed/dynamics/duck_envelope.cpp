/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "dsp-next/fixed/dynamics/duck_envelope.h"

#include "dsp-next/fixed/interpolation/table_interpolation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {

// The helpers sit in a named namespace inside the anonymous one: the backend is a unity build, so another file's
// anonymous helpers share this translation unit.
namespace {
namespace duck {

using Stage = DuckEnvelope::Stage;
using State = DuckEnvelope::State;

/// The decay tables are read across one whole stage: the remaining distance has the stage's 23 position bits.
constexpr uint8_t kDecayTableInputBits = DuckEnvelope::kStagePositionBits;
static_assert(DuckEnvelope::kStageLength == uint32_t{1} << kDecayTableInputBits);

/// One curvedness unit, fully curved, at 2^16; the release blends straight and curved by it.
constexpr int32_t kCurvednessUnit = int32_t{1} << 16;
/// The shift from a position (2^23 per stage) to curvedness units (2^16), so the curvedness falls by one unit across
/// the stage.
constexpr uint32_t kPositionToCurvednessShift = 7;
/// The shift from a position (2^23 per stage) to a Q15 fraction of the stage, which a weight of up to 2^16 scales to
/// Q31.
constexpr uint32_t kPositionToQ15Shift = 8;
/// The shift from a position (2^23 per stage) to a Q31 fraction of the stage.
constexpr uint32_t kPositionToQ31Shift = 8;
/// The shift from a decay-table value (Q31) to Q15, which a weight of up to 2^16 scales back to Q31.
constexpr uint32_t kDecayToQ15Shift = 16;

/// @return @p a times @p b, both Q31, at Q31: the high word of the product is Q30, shifted up by one.
[[gnu::always_inline]] inline q31 multiply_q31(q31 a, q31 b) {
	return multiply_32x32_rshift32(a, b) << 1;
}

/// @return The decay table's remaining value at @p pos into a stage: about 0 at the start, full scale at the end.
[[gnu::always_inline]] inline q31 remaining_decay4(uint32_t pos) {
	return getDecay4(DuckEnvelope::kStageLength - pos, kDecayTableInputBits);
}

/// @return As @ref remaining_decay4, on the 8-curve table.
[[gnu::always_inline]] inline q31 remaining_decay8(uint32_t pos) {
	return getDecay8(DuckEnvelope::kStageLength - pos, kDecayTableInputBits);
}

/// @brief Enters @p next at its start: the attack falls from the level held to the floor, the release rises from the
/// floor to ONE_Q31, and off holds nothing.
void enter(State& s, Stage next) {
	s.stage = next;
	switch (next) {
	case Stage::ATTACK:
		s.pos = 0;
		s.depth = s.level - s.floor;
		break;
	case Stage::RELEASE:
		s.pos = 0;
		s.depth = ONE_Q31 - s.floor;
		break;
	case Stage::OFF:
		break;
	}
}

/// @brief Moves the current stage on by @p n samples of @p step each, modulo 2^32.
/// @return Whether the stage is still under way: its position short of the stage's length.
[[gnu::always_inline]] inline bool step_stage(State& s, uint32_t step, std::size_t n) {
	s.pos += static_cast<uint32_t>(n) * step;
	return s.pos < DuckEnvelope::kStageLength;
}

/// @return Whether a stage of @p step per sample is still under way @p samples after it began: before its length over
/// the step, in whole samples rounded down. A zero step never ends.
[[gnu::always_inline]] inline bool lasts_past(uint32_t step, uint32_t samples) {
	return step == 0 || samples < DuckEnvelope::kStageLength / step;
}

} // namespace duck
} // namespace

q31 attack_level(q31 depth, q31 floor, uint32_t pos) {
	const q31 travelled = ONE_Q31 - duck::remaining_decay4(pos);
	return duck::multiply_q31(depth, travelled) + floor;
}

q31 release_level(q31 depth, uint32_t pos, int32_t curve_extent16) {
	const int32_t curvedness = curve_extent16 - static_cast<int32_t>(pos >> duck::kPositionToCurvednessShift);

	q31 travelled = 0;
	if (curvedness < 0) {
		travelled = static_cast<q31>(pos << duck::kPositionToQ31Shift);
	}
	else {
		const int32_t curved = std::min(curvedness, duck::kCurvednessUnit);
		const int32_t straight = duck::kCurvednessUnit - curved;
		travelled = (straight * static_cast<int32_t>(pos >> duck::kPositionToQ15Shift))
		            + ((duck::remaining_decay8(pos) >> duck::kDecayToQ15Shift) * curved);
	}

	return ONE_Q31 - depth + duck::multiply_q31(travelled, depth);
}

q31 DuckEnvelope::advance(const Coefficients& c, State& s, std::size_t n) {
	if (!s.pending.empty()) {
		const q31 floor = ONE_Q31 - s.pending.strength;
		s.pending = {};
		if (floor < s.level) {
			s.floor = floor;
			duck::enter(s, c.instant_attack ? Stage::RELEASE : Stage::ATTACK);
		}
	}

	if (s.stage == Stage::ATTACK) {
		if (duck::step_stage(s, c.attack_step, n)) {
			s.level = attack_level(s.depth, s.floor, s.pos);
			return s.level - ONE_Q31;
		}
		duck::enter(s, Stage::RELEASE);
	}

	if (s.stage == Stage::RELEASE) {
		if (duck::step_stage(s, c.release_step, n)) {
			s.level = release_level(s.depth, s.pos, c.curve_extent16);
			return s.level - ONE_Q31;
		}
		duck::enter(s, Stage::OFF);
	}

	s.level = ONE_Q31;
	return 0;
}

void DuckEnvelope::hit(State& s, q31 strength) {
	s.pending = s.pending.with(strength);
}

void DuckEnvelope::hit_at(const Coefficients& c, State& s, q31 strength, uint32_t samples_ago) {
	s.pending = {};
	s.floor = ONE_Q31 - strength;
	s.depth = strength;

	if (duck::lasts_past(c.attack_step, samples_ago)) {
		s.stage = Stage::ATTACK;
		s.pos = samples_ago * c.attack_step;
		return;
	}

	const uint32_t since_release = samples_ago - (kStageLength / c.attack_step);
	if (duck::lasts_past(c.release_step, since_release)) {
		s.stage = Stage::RELEASE;
		s.pos = since_release * c.release_step;
		return;
	}

	s.stage = Stage::OFF;
}

void DuckEnvelope::stop(State& s) {
	s.stage = Stage::OFF;
}

} // namespace deluge::dsp::fixed
