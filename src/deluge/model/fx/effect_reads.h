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

/// @file
/// @brief An owner's unpatched effect params read by its table's ids: the words and knobs the effects take.

#include "effects/bitcrush.h"
#include "effects/equalizer.h"
#include "effects/filter.h"
#include "effects/mod_fx.h"
#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "model/project_bridge/param_reads.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "plugin/types/param.h"

#include <array>
#include <cassert>
#include <cstdint>

namespace deluge::model {

using bridge::unpatchedIndex;

/// @brief The filters' words, in effects::Filter::Words order.
/// @param set The owner's unpatched params.
/// @param ids The owner's filter ids (host::chains::kFilterIds).
/// @return The words.
[[nodiscard]] inline effects::Filter::Words filterWords(UnpatchedParamSet& set,
                                                        const std::array<plugin::ParamId, 6>& ids) {
	return {
	    .lpf_frequency = set.getValue(unpatchedIndex(ids[0])),
	    .lpf_resonance = set.getValue(unpatchedIndex(ids[1])),
	    .lpf_morph = set.getValue(unpatchedIndex(ids[2])),
	    .hpf_frequency = set.getValue(unpatchedIndex(ids[3])),
	    .hpf_resonance = set.getValue(unpatchedIndex(ids[4])),
	    .hpf_morph = set.getValue(unpatchedIndex(ids[5])),
	};
}

/// @brief The mod FX's words, in effects::ModFx::Words order.
/// @param set The owner's unpatched params.
/// @param ids The owner's mod FX ids (host::chains::kModFxIds).
/// @return The words.
[[nodiscard]] inline effects::ModFx::Words modFxWords(UnpatchedParamSet& set,
                                                      const std::array<plugin::ParamId, 4>& ids) {
	return {
	    .rate = set.getValue(unpatchedIndex(ids[0])),
	    .depth = set.getValue(unpatchedIndex(ids[1])),
	    .offset = set.getValue(unpatchedIndex(ids[2])),
	    .feedback = set.getValue(unpatchedIndex(ids[3])),
	};
}

/// @brief The EQ's knobs, in effects::EqualizerParams order.
/// @param set The owner's unpatched params.
/// @param ids The owner's EQ ids (host::chains::kEqIds, instruments::classic::kEqIds).
/// @return The knobs.
[[nodiscard]] inline effects::EqualizerParams eqKnobs(UnpatchedParamSet& set,
                                                      const std::array<plugin::ParamId, 4>& ids) {
	return {
	    .bass = set.getKnob(unpatchedIndex(ids[0])),
	    .treble = set.getKnob(unpatchedIndex(ids[1])),
	    .bass_frequency = set.getKnob(unpatchedIndex(ids[2])),
	    .treble_frequency = set.getKnob(unpatchedIndex(ids[3])),
	};
}

/// @brief The bitcrush's knobs, in effects::BitcrushParams order.
/// @param set The owner's unpatched params.
/// @param ids The owner's bitcrush ids (host::chains::kBitcrushIds, instruments::classic::kBitcrushIds).
/// @return The knobs.
[[nodiscard]] inline effects::BitcrushParams bitcrushKnobs(UnpatchedParamSet& set,
                                                           const std::array<plugin::ParamId, 2>& ids) {
	return {
	    .bitcrush = set.getKnob(unpatchedIndex(ids[0])),
	    .sample_rate_reduction = set.getKnob(unpatchedIndex(ids[1])),
	};
}

} // namespace deluge::model
