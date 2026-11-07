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

#include "definitions_cxx.hpp"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include "effects/filter.h"
#include "effects/master_chain.h"
#include "effects/track_chain.h"
#include "model/fx/chain_conversions.h"
#include "model/snapshot/mod_controllable_snapshot.h"

#include <cstdint>

class ParamManager;
class Song;
class UnpatchedParamSet;

namespace deluge::model {

class TrackChannel;

/// @brief An output's pitch adjust knob as the factor its content's pitch takes: its exponential curve around unison,
/// the knob's ±1 being ±4 octaves.
/// @param knob The pitch adjust param's stored word.
/// @return The pitch factor, Q24: 2^24 unison.
[[nodiscard]] inline dsp::Q<24> pitch_adjust_from_knob(dsp::Q<31> knob) {
	// The knob's word ≫ 3 is its octaves at Q26
	return dsp::fixed::exp_curve(dsp::Q<24>::one(), dsp::reinterpret_bits<dsp::Q<26>>(scale_shr_floor<3>(knob)));
}

/// @brief An output's volume knob as the volume its chain renders at: the knob's square curve around unity, halved, as
/// the curve's neutral doubles already (its maximum is 4).
/// @param knob The volume param's stored word.
/// @return The volume, Q27: 2^27 unity.
[[nodiscard]] inline dsp::Q<27> volume_from_knob(dsp::Q<31> knob) {
	return halve_floor(dsp::fixed::curve_square(dsp::Q<27>::one(), dsp::fixed::preset_combination(knob)));
}

/// @brief A host chain's filter params for one block: each filter's final values from its unpatched params, the
/// params' own values, and whether the CPU allows the drive ladder to oversample.
/// @param unpatched The owner's unpatched param set.
/// @param conversions The owner's remembered conversions.
/// @return The block's filter params.
[[nodiscard]] effects::FilterParams chainFilterParams(UnpatchedParamSet& unpatched, ChainConversions& conversions);

/// @brief The mod FX, EQ and delay inputs a host chain reads from its unpatched params: the type in effect (NONE in its
/// soft off), the rate and depth finals and the knobs.
/// @param snapshot The chain's render copy of its settings and view.
/// @param activeType The mod FX type in effect this block (see activeModFXType).
/// @param paramManager The owner's param manager.
/// @param delay The block's delay working state, set up on the owner's chain's delay.
/// @param grainSend The granular's reverb send.
/// @param conversions The owner's remembered conversions.
/// @return The block's mod FX, EQ and delay inputs.
[[nodiscard]] effects::FxInputs hostFxInputs(const snapshot::ModControllableSnapshot& snapshot, ModFXType activeType,
                                             ParamManager& paramManager, const effects::Delay::State& delay,
                                             dsp::Q<31> grainSend, ChainConversions& conversions);

/// @brief The mod FX type a host chain runs this block: NONE while its selected mod FX knob is at its minimum (the soft
/// off), else its setting.
/// @param snapshot The chain's render copy of its settings and view.
/// @param paramManager The owner's param manager.
/// @return The mod FX type in effect.
[[nodiscard]] ModFXType activeModFXType(const snapshot::ModControllableSnapshot& snapshot, ParamManager& paramManager);

/// @brief A host chain's delay working state for one block, from its unpatched params, set up on @p delay.
/// @param paramManager The owner's param manager.
/// @param delay The chain's delay.
/// @param settings The delay's settings, from the chain's render copy.
/// @param shouldLimitDelayFeedback Whether the feedback is capped below self-oscillation.
/// @param soundComingIn Whether the content rendered last block: the delay starts or stops on it.
/// @param conversions The owner's remembered conversions.
/// @return The block's delay working state.
[[nodiscard]] effects::Delay::State delayWorkingState(ParamManager& paramManager, effects::Delay& delay,
                                                      const effects::DelayState& settings,
                                                      bool shouldLimitDelayFeedback, bool soundComingIn,
                                                      ChainConversions& conversions);

/// @brief A track chain's sidechain volume param as a patch cable's depth.
/// @param paramManager The owner's param manager.
/// @return The depth: the knob plus one.
[[nodiscard]] dsp::Q<29> sidechainDepth(ParamManager& paramManager);

/// @brief A kit's or an audio track's chain inputs for one block, from its unpatched params.
/// @param channel The kit's or audio track's channel.
/// @param paramManager The owner's param manager.
/// @param volumeAdjustment The output's volume, as the drums are rendered at (volume_from_knob).
/// @param reverbAmountAdjust The song's reverb send scaling for this output, Q31.
/// @param sideChainHitPending The sidechain hit pending this block, Q31; 0 for none.
/// @param delay The block's delay working state, set up on the owner's chain's delay.
/// @param outputType KIT or AUDIO; selects the volume headroom factor applied after the filters.
/// @param conversions The channel's remembered conversions.
/// @return The block's track-chain inputs.
[[nodiscard]] effects::TrackChainInputs trackChainInputs(const TrackChannel& channel, ParamManager& paramManager,
                                                         dsp::Q<27> volumeAdjustment, int32_t reverbAmountAdjust,
                                                         int32_t sideChainHitPending,
                                                         const effects::Delay::State& delay, OutputType outputType,
                                                         ChainConversions& conversions);

/// @brief The song master's half before the reverb, from the song's params once its outputs have rendered; sets up the
/// block's delay working state on the master's delay.
/// @param song The song whose master chain is read.
/// @param volumePostFX The song's post-FX volume, as its outputs were rendered at.
/// @return The master chain's pre-reverb inputs.
[[nodiscard]] effects::MasterChainInputs masterChainInputs(Song& song, dsp::Q<27> volumePostFX);

/// @brief The song master's half after the reverb, from the song's params once the reverb has rendered.
/// @param song The song whose master chain is read.
/// @return The master chain's post-reverb inputs.
[[nodiscard]] effects::MasterPostInputs masterPostInputs(Song& song);

} // namespace deluge::model
