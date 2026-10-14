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

#include "processing/sound/sound_chain_inputs.h"

#include "model/fx/song_stutter.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

namespace params = deluge::modulation::params;

deluge::effects::SoundChainInputs soundChainInputs(Sound& sound, ParamManager& paramManager,
                                                   const deluge::effects::Delay::State& delay, bool anyVoices,
                                                   int32_t reverbSendAmount) {
	const auto& finals = sound.params.paramFinalValues;
	UnpatchedParamSet* unpatched = paramManager.getUnpatchedParamSet();
	const deluge::model::StutterInput stutter = deluge::model::stutterInput(sound, &paramManager);
	return {
	    .fx =
	        {
	            .settings = &sound.modControllableSnapshot().effects,
	            .mod_fx_type = sound.modControllableSnapshot().effects.mod_fx.type,
	            .mod_fx_rate = finals[params::GLOBAL_MOD_FX_RATE - params::FIRST_GLOBAL],
	            .mod_fx_depth = finals[params::GLOBAL_MOD_FX_DEPTH - params::FIRST_GLOBAL],
	            .mod_fx_offset = unpatched->getValue(params::UNPATCHED_MOD_FX_OFFSET),
	            .mod_fx_feedback = unpatched->getValue(params::UNPATCHED_MOD_FX_FEEDBACK),
	            .eq = {.bass = unpatched->getValue(params::UNPATCHED_BASS),
	                   .treble = unpatched->getValue(params::UNPATCHED_TREBLE),
	                   .bass_frequency = unpatched->getValue(params::UNPATCHED_BASS_FREQ),
	                   .treble_frequency = unpatched->getValue(params::UNPATCHED_TREBLE_FREQ)},
	            .delay = delay,
	            .stereo = AudioEngine::renderInStereo,
	            .grain_send = reverbSendAmount >> 1,
	        },
	    .bitcrush = {.bitcrush = unpatched->getValue(params::UNPATCHED_BITCRUSHING),
	                 .sample_rate_reduction = unpatched->getValue(params::UNPATCHED_SAMPLE_RATE_REDUCTION)},
	    .post_fx_volume = finals[params::GLOBAL_VOLUME_POST_FX - params::FIRST_GLOBAL],
	    .post_reverb_volume = finals[params::GLOBAL_VOLUME_POST_REVERB_SEND - params::FIRST_GLOBAL],
	    .reverb_send = reverbSendAmount,
	    .sound = anyVoices,
	    .compressor_threshold = unpatched->getValue(params::UNPATCHED_COMPRESSOR_THRESHOLD),
	    .stutter = stutter.stutter,
	    .stutter_rate = stutter.rate,
	};
}
