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
#include <optional>
#include <span>
#include <utility>

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

/// @brief Each of the DX7's own params beside the oscillator A param a DX7 on oscillator 1 took it from (its level the
/// volume, its amplitude modulation the pulse width, its pitch adjust the pitch adjust): what stands in for which when
/// a sound moves between DX7 mode and the oscillator engines.
inline constexpr std::array<std::pair<plugin::ParamId, plugin::ParamId>, 3> kDx7Counterparts{{
    {param::kOscAVolume, param::kDx7Level},
    {param::kOscAPhaseWidth, param::kDx7AmpMod},
    {param::kOscAPitch, param::kDx7Pitch},
}};

/// @brief The DX7 param oscillator A's param @p osc_a stands in for, or nullopt for a param with none.
/// @param osc_a A param id of the instrument.
/// @return The DX7's counterpart.
[[nodiscard]] constexpr std::optional<plugin::ParamId> dx7_counterpart(plugin::ParamId osc_a) {
	for (const auto& [osc, dx7] : kDx7Counterparts) {
		if (osc == osc_a) {
			return dx7;
		}
	}
	return std::nullopt;
}

/// @brief The oscillator A param the DX7's param @p dx7 stands in for, or nullopt for a param with none.
/// @param dx7 A param id of the instrument.
/// @return Oscillator A's counterpart.
[[nodiscard]] constexpr std::optional<plugin::ParamId> osc_a_counterpart(plugin::ParamId dx7) {
	for (const auto& [osc, own] : kDx7Counterparts) {
		if (own == dx7) {
			return osc;
		}
	}
	return std::nullopt;
}

/// @brief The param @p id names in @p mode: itself while live; else its counterpart, the DX7's for an oscillator A
/// param in DX7 mode, oscillator A's for a DX7 param in an oscillator engine's mode; nullopt for a parked param with
/// none.
/// @param mode The sound's synth mode.
/// @param id   A param id of the instrument.
/// @return The live param @p id names.
[[nodiscard]] constexpr std::optional<plugin::ParamId> playing_param(SynthMode mode, plugin::ParamId id);

/// @brief Whether @p target, a param or a cable's depth, is live in @p mode: a depth is live while the param its chain
/// of cables roots at is.
/// @param mode   The sound's synth mode.
/// @param target A param id or a cable depth id.
/// @return True when @p target is live in @p mode.
[[nodiscard]] constexpr bool target_is_live(SynthMode mode, plugin::ParamId target) {
	return param_is_live(mode, is_cable_depth(target) ? cable_root(target) : target);
}

[[nodiscard]] constexpr std::optional<plugin::ParamId> playing_param(SynthMode mode, plugin::ParamId id) {
	if (param_is_live(mode, id)) {
		return id;
	}
	const std::optional<plugin::ParamId> other = mode == SynthMode::DX7 ? dx7_counterpart(id) : osc_a_counterpart(id);
	if (other && param_is_live(mode, *other)) {
		return other;
	}
	return std::nullopt;
}

/// @brief @c playing_param for @p target, a param or a cable's depth: a depth names the same cables onto the param
/// its root stands in for.
/// @param mode   The sound's synth mode.
/// @param target A param id or a cable depth id.
/// @return The live target @p target names, or nullopt.
[[nodiscard]] constexpr std::optional<plugin::ParamId> playing_target(SynthMode mode, plugin::ParamId target) {
	if (!is_cable_depth(target)) {
		return playing_param(mode, target);
	}
	const std::optional<plugin::ParamId> root = playing_param(mode, cable_root(target));
	if (!root) {
		return std::nullopt;
	}
	return plugin::ParamId{(target.value & ~0xFFu) | root->value};
}

} // namespace deluge::instruments::classic
