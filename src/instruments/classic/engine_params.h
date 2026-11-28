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

#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"
#include "instruments/classic/words.h"
#include "plugin/types/param.h"
#include "synth/engines/engine_table.h"
#include "synth/table_rows.h"

#include <algorithm>
#include <array>
#include <span>

/// @file
/// @brief Which of the Deluge instrument's params each engine owns. A param no engine row owns is the sound's (its
/// voice chain, effects, envelopes, LFOs, the arp's rate) and is live in every mode; an engine's own params are live,
/// reachable and saved only while that engine plays. The engines' params keep their values while another engine plays.

namespace deluge::instruments::classic {

/// @brief The params the oscillator engines (subtractive, ring modulation, FM) read: the two slots' volume, pitch
/// adjust, pulse width and wavetable position, the noise, and FM's carriers' feedback and its modulators. The DX7 reads
/// none of them.
inline constexpr std::array kOscillatorEngineParams{
    param::kOscAVolume,
    param::kOscBVolume,
    param::kNoiseVolume,
    param::kModulator1Volume,
    param::kModulator2Volume,
    param::kModulator1Feedback,
    param::kModulator2Feedback,
    param::kCarrier1Feedback,
    param::kCarrier2Feedback,
    param::kOscAPhaseWidth,
    param::kOscBPhaseWidth,
    param::kOscAWavetablePosition,
    param::kOscBWavetablePosition,
    param::kOscAPitch,
    param::kOscBPitch,
    param::kModulator1Pitch,
    param::kModulator2Pitch,
};

/// @brief The params the DX7 engine reads of its own: its level, amplitude modulation and pitch adjust.
inline constexpr std::array kDx7EngineParams{param::kDx7Level, param::kDx7AmpMod, param::kDx7Pitch};

/// @brief The params engine row @p Row owns.
template <class Row>
struct EngineParams;

template <>
struct EngineParams<synth::SubtractiveRow> {
	static constexpr std::span<const plugin::ParamId> kOwned{kOscillatorEngineParams};
};

template <>
struct EngineParams<synth::RingModRow> {
	static constexpr std::span<const plugin::ParamId> kOwned{kOscillatorEngineParams};
};

template <>
struct EngineParams<synth::FmRow> {
	static constexpr std::span<const plugin::ParamId> kOwned{kOscillatorEngineParams};
};

template <>
struct EngineParams<synth::Dx7Row> {
	static constexpr std::span<const plugin::ParamId> kOwned{kDx7EngineParams};
};

/// @brief Whether the instrument's param @p id is reachable and saved in @p mode: the sound's own params always, an
/// engine's only while it plays.
/// @param mode The sound's synth mode.
/// @param id   A param id of the instrument.
/// @return True when @p id is live in @p mode.
[[nodiscard]] constexpr bool param_is_live(SynthMode mode, plugin::ParamId id) {
	bool owned = false;
	bool live = false;
	synth::for_each_row<synth::EngineRows>([&]<class Row>(Row) {
		if (std::ranges::find(EngineParams<Row>::kOwned, id) != EngineParams<Row>::kOwned.end()) {
			owned = true;
			live = live || Row::kMode == mode;
		}
	});
	return !owned || live;
}

/// @brief Whether @p target, a param or a cable's depth, is live in @p mode: a depth is live while the param its chain
/// of cables roots at is.
/// @param mode   The sound's synth mode.
/// @param target A param id or a cable depth id.
/// @return True when @p target is live in @p mode.
[[nodiscard]] constexpr bool target_is_live(SynthMode mode, plugin::ParamId target) {
	return param_is_live(mode, is_cable_depth(target) ? cable_root(target) : target);
}

} // namespace deluge::instruments::classic
