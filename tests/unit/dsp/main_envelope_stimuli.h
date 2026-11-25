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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

/// @file
/// @brief The stimuli main's envelope was recorded under (`tools/envelope_records/`), shared by the probe that drives
/// main's envelope and the specs that drive next's, so both read the same events at the same blocks. Standalone: no
/// project header, since the probe builds it against main's sources.

namespace deluge::test::main_envelope {

/// @brief main's EnvelopeMode, by number.
enum class Mode : uint8_t { ADSR, AHD, AR, MPE };

/// @brief The follow input's course over a case, as a Q31 patch source word.
enum class Follow : uint8_t {
	STILL_ZERO, ///< 0 throughout
	RAMP,       ///< 0 to full scale across the case
	STEP,       ///< 0, then full scale from the middle block
	STILL_HALF, ///< half scale throughout
};

/// @brief A block that no event uses.
inline constexpr uint32_t kNever = std::numeric_limits<uint32_t>::max();

/// @brief The per-sample rate that makes a stage last @p samples (main's stage is 2^23 position steps).
[[nodiscard]] constexpr uint32_t rate_for(uint32_t samples) {
	return 8388608U / samples;
}

/// @brief One case: a note under one mode and curve, rendered in blocks, with the events at the blocks named.
struct Stimulus {
	Mode mode;
	int8_t curve;
	uint32_t block;          ///< samples per render
	uint32_t blocks;         ///< renders
	uint32_t attack;         ///< per-sample rates
	uint32_t decay;          ///< (AHD: the hold)
	uint32_t release;        ///< (AHD: the decay)
	int32_t sustain;         ///< Q31 level (AR, MPE: the level)
	bool directly_to_decay;  ///< the note-on skips the attack
	bool has_sustain;        ///< ADSR's note-off releases (main's envelopeHasSustainCurrently)
	uint32_t rate_change;    ///< from this block every rate is halved (stages twice as long)
	uint32_t note_off;       ///< a note-off before this block
	uint32_t fast_release;   ///< a fast release (soft cull) before this block
	uint32_t retrigger;      ///< a note-on resuming from the current level before this block
	Follow follow;           ///< the follow input
	uint32_t sustain_change; ///< from this block the sustain is a quarter of full scale
};

/// @brief The follow input of @p s at block @p b.
[[nodiscard]] constexpr int32_t follow_at(const Stimulus& s, uint32_t b) {
	constexpr int64_t kFull = std::numeric_limits<int32_t>::max();
	switch (s.follow) {
	case Follow::RAMP:
		return static_cast<int32_t>((kFull * b) / (s.blocks - 1));
	case Follow::STEP:
		return b >= s.blocks / 2 ? static_cast<int32_t>(kFull) : 0;
	case Follow::STILL_HALF:
		return static_cast<int32_t>(kFull / 2);
	default:
		return 0;
	}
}

/// @brief The rates and sustain of @p s at block @p b, after the rate and sustain changes.
struct Params {
	uint32_t attack, decay, release;
	int32_t sustain;
};
[[nodiscard]] constexpr Params params_at(const Stimulus& s, uint32_t b) {
	const bool slower = b >= s.rate_change;
	return {.attack = slower ? s.attack / 2 : s.attack,
	        .decay = slower ? s.decay / 2 : s.decay,
	        .release = slower ? s.release / 2 : s.release,
	        .sustain = b >= s.sustain_change ? 0x2000'0000 : s.sustain};
}

/// @brief The curves every mode is recorded at.
inline constexpr std::array<int8_t, 7> kCurves{-50, -20, 0, 1, 16, 32, 50};

/// A note over 4,096 samples in blocks of @p block: attack 600 samples, decay 900, release 1,200, sustain half, the
/// rates halved at 1,536 samples and the note-off at 2,560.
[[nodiscard]] constexpr Stimulus base(Mode mode, int8_t curve, uint32_t block) {
	const uint32_t blocks = 4096 / block;
	return {.mode = mode,
	        .curve = curve,
	        .block = block,
	        .blocks = blocks,
	        .attack = rate_for(600),
	        .decay = rate_for(900),
	        .release = rate_for(1200),
	        .sustain = 0x4000'0000,
	        .directly_to_decay = false,
	        .has_sustain = true,
	        .rate_change = (blocks * 3) / 8,
	        .note_off = (blocks * 5) / 8,
	        .fast_release = kNever,
	        .retrigger = kNever,
	        .follow = mode == Mode::MPE ? Follow::RAMP : Follow::STILL_ZERO,
	        .sustain_change = kNever};
}

inline constexpr size_t kModes = 4;
inline constexpr size_t kEventCases = 8;
inline constexpr size_t kCount = (kModes * kCurves.size()) + (kModes * 2) + (kModes * kEventCases);

/// @brief Every case: each mode at each curve in blocks of 32; each mode at the classic curve in blocks of 1 and 128;
/// and each mode, classic, in blocks of 32 under each event.
[[nodiscard]] constexpr std::array<Stimulus, kCount> stimuli() {
	std::array<Stimulus, kCount> out{};
	size_t i = 0;
	for (size_t m = 0; m < kModes; ++m) {
		const auto mode = static_cast<Mode>(m);
		for (int8_t curve : kCurves) {
			out[i++] = base(mode, curve, 32);
		}
		out[i++] = base(mode, 32, 1);
		out[i++] = base(mode, 32, 128);
		Stimulus s = base(mode, 32, 32);
		s.note_off = 10; // mid-attack
		out[i++] = s;
		s = base(mode, 32, 32);
		s.note_off = 30; // mid-decay
		out[i++] = s;
		s = base(mode, 32, 32);
		s.retrigger = 100; // mid-release
		out[i++] = s;
		s = base(mode, 32, 32);
		s.fast_release = 60;
		out[i++] = s;
		s = base(mode, 32, 32);
		s.directly_to_decay = true;
		out[i++] = s;
		s = base(mode, 32, 32);
		s.has_sustain = false; // ADSR ignores the note-off; the others decide for themselves
		s.sustain = 0;
		out[i++] = s;
		s = base(mode, 32, 32);
		s.follow = Follow::STEP;
		s.sustain_change = 40;
		out[i++] = s;
		s = base(mode, 32, 32);
		s.follow = Follow::STILL_HALF;
		s.sustain = 0;
		out[i++] = s;
	}
	return out;
}

inline constexpr std::array<Stimulus, kCount> kStimuli = stimuli();

} // namespace deluge::test::main_envelope
