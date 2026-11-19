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

#include "definitions_cxx.hpp" // OscType, SampleRepeatMode
#include "dsp-next/core/fixed_point.h"
#include "plugin-api/types/interpolation_mode.h"
#include "synth/sources/input/input_channel.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace deluge::synth {

/// @brief A pitch offset: semitones and cents.
struct Tuning {
	int16_t transpose{0}; ///< semitones
	int8_t cents{0};      ///< −50..50
	bool operator==(const Tuning&) const = default;
};

/// @brief Where an oscillator's phase starts a note: none for one that runs free.
using Retrigger = std::optional<dsp::UQ<32>>;

/// @brief An oscillator's pitch: its tuning and where each note starts it.
struct Pitch {
	Tuning tuning;
	Retrigger retrigger;
	bool operator==(const Pitch&) const = default;
};

/// @brief A virtual-analog oscillator's wave.
enum class VaShape : uint8_t { SINE, TRIANGLE, SQUARE, ANALOG_SQUARE, SAW, ANALOG_SAW, SUPERSAW };

/// @brief What a virtual-analog oscillator saves.
struct VaState {
	Pitch pitch;
	VaShape shape{VaShape::SQUARE};
	bool operator==(const VaState&) const = default;
};

/// @brief One wavetable range: the highest note it covers and its file.
struct WavetableRange {
	int16_t top_note{0};
	std::string file_path;
	bool operator==(const WavetableRange&) const = default;
};

/// @brief What a wavetable oscillator saves.
struct WavetableState {
	Pitch pitch;
	std::vector<WavetableRange> ranges;
	bool operator==(const WavetableState&) const = default;
};

/// @brief The part of a sample a range plays, in samples; a loop point of 0 is none.
struct SampleZone {
	uint64_t start{0};
	uint64_t end{0};
	uint32_t loop_start{0};
	uint32_t loop_end{0};
	bool operator==(const SampleZone&) const = default;
};

/// @brief One sample range: the highest note it covers, its file, the zone it plays and its own tuning.
struct SampleRange {
	int16_t top_note{0};
	std::string file_path;
	SampleZone zone;
	Tuning tuning;
	bool operator==(const SampleRange&) const = default;
};

/// @brief How a sample oscillator plays its ranges.
struct SamplePlayback {
	SampleRepeatMode repeat_mode{SampleRepeatMode::CUT};
	int8_t time_stretch_amount{0};
	plugin::InterpolationMode interpolation{plugin::InterpolationMode::SMOOTH};
	bool pitch_and_speed_independent{false};
	bool reversed{false};
	bool operator==(const SamplePlayback&) const = default;
};

/// @brief What a sample oscillator saves: its ranges carry their tuning, so it has no pitch of its own; where its
/// phase starts a note it still has.
struct SampleState {
	SamplePlayback playback;
	std::vector<SampleRange> ranges;
	Retrigger retrigger;
	bool operator==(const SampleState&) const = default;
};

/// @brief A DX7 voice: its 156 bytes, the engine it renders in (0 auto, 1 modern, 2 MkI) and its random detune.
struct DxPatch {
	std::array<uint8_t, 156> params{};
	uint8_t engine_mode{0};
	int32_t random_detune{0};
	bool operator==(const DxPatch&) const = default;
};

/// @brief What a DX7 oscillator saves: its voice, none for one read without its own (its runtime makes the init
/// voice, which loading records).
struct Dx7State {
	Pitch pitch;
	std::optional<DxPatch> patch;
	bool operator==(const Dx7State&) const = default;
};

/// @brief What a live-input oscillator saves.
struct InputState {
	Pitch pitch;
	InputChannel channel{InputChannel::LEFT};
	bool operator==(const InputState&) const = default;
};

/// @brief What one oscillator slot saves: one kind's state.
using SlotState = std::variant<VaState, WavetableState, SampleState, Dx7State, InputState>;

} // namespace deluge::synth
