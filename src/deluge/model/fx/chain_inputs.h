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
#include "effects/filter.h"
#include "effects/master_chain.h"
#include "effects/track_chain.h"
#include "model/snapshot/mod_controllable_snapshot.h"

#include <cstdint>

class ModControllableAudio;
class ParamManager;
class Song;
class UnpatchedParamSet;

namespace deluge::model {

class TrackChannel;

/// @brief A host chain's filter params for one block: each filter's final values from its unpatched params, the
/// params' own values, and whether the CPU allows the drive ladder to oversample.
/// @param unpatched The owner's unpatched param set.
/// @return The block's filter params.
[[nodiscard]] effects::FilterParams chainFilterParams(UnpatchedParamSet& unpatched);

/// @brief The mod FX, EQ and delay inputs a host chain reads from its unpatched params: the type in effect (NONE in its
/// soft off), the rate and depth finals and the knobs.
/// @param owner The host chain's owner, which supplies its effect settings and active mod FX type.
/// @param paramManager The owner's param manager.
/// @param delay The block's delay working state, set up on the owner's chain's delay.
/// @param grainSend The granular's reverb send, Q31.
/// @return The block's mod FX, EQ and delay inputs.
[[nodiscard]] effects::FxInputs hostFxInputs(const snapshot::ModControllableSnapshot& snapshot, ModFXType activeType,
                                             ParamManager& paramManager, const effects::Delay::State& delay,
                                             int32_t grainSend);

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
/// @return The block's delay working state.
[[nodiscard]] effects::Delay::State delayWorkingState(ParamManager& paramManager, effects::Delay& delay,
                                                      const effects::DelayState& settings,
                                                      bool shouldLimitDelayFeedback, bool soundComingIn);

/// @brief A track chain's sidechain volume param as a patch cable's depth.
/// @param paramManager The owner's param manager.
/// @return The depth, Q29.
[[nodiscard]] int32_t sidechainDepth(ParamManager& paramManager);

/// @brief A kit's or an audio track's chain inputs for one block, from its unpatched params.
/// @param channel The kit's or audio track's channel.
/// @param stutterOwner The kit or audio track, which the song's stutter knows it by.
/// @param paramManager The owner's param manager.
/// @param volumeAdjustment The output's volume param's final, halved (Q31), as the drums are rendered at.
/// @param reverbAmountAdjust The song's reverb send scaling for this output, Q31.
/// @param sideChainHitPending The sidechain hit pending this block, Q31; 0 for none.
/// @param delay The block's delay working state, set up on the owner's chain's delay.
/// @param outputType KIT or AUDIO; selects the volume headroom factor applied after the filters.
/// @return The block's track-chain inputs.
[[nodiscard]] effects::TrackChainInputs trackChainInputs(TrackChannel& channel,
                                                         const ModControllableAudio& stutterOwner,
                                                         ParamManager& paramManager, int32_t volumeAdjustment,
                                                         int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                                         const effects::Delay::State& delay, OutputType outputType);

/// @brief The song master's half before the reverb, from the song's params once its outputs have rendered; sets up the
/// block's delay working state on the master's delay.
/// @param song The song whose master chain is read.
/// @param volumePostFX The song's post-FX volume, Q31, as its outputs were rendered at.
/// @return The master chain's pre-reverb inputs.
[[nodiscard]] effects::MasterChainInputs masterChainInputs(Song& song, int32_t volumePostFX);

/// @brief The song master's half after the reverb, from the song's params once the reverb has rendered.
/// @param song The song whose master chain is read.
/// @return The master chain's post-reverb inputs.
[[nodiscard]] effects::MasterPostInputs masterPostInputs(Song& song);

} // namespace deluge::model
