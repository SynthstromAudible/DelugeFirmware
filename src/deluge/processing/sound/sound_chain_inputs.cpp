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
#include "util/synth_noise.h"

#include "model/fx/song_stutter.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "synth/matrix/param_format.h"

namespace params = deluge::modulation::params;

deluge::effects::SoundChainInputs soundChainInputs(Sound& sound, ParamManager& paramManager,
                                                   const deluge::effects::Delay::State& delay, bool anyVoices,
                                                   deluge::dsp::Q<31> reverbSendAmount) {
	using deluge::synth::matrix::global_param_value;
	const auto& finals = sound.params.param_final_values;
	UnpatchedParamSet* unpatched = paramManager.getUnpatchedParamSet();
	const deluge::model::StutterInput stutter = deluge::model::stutterInput(sound.core(), &paramManager);
	return {
	    .fx =
	        {
	            .settings = &sound.modControllableSnapshot().effects,
	            .mod_fx_type = sound.modControllableSnapshot().effects.mod_fx.type,
	            .mod_fx_rate = finals[params::GLOBAL_MOD_FX_RATE - params::FIRST_GLOBAL],
	            .mod_fx_depth = finals[params::GLOBAL_MOD_FX_DEPTH - params::FIRST_GLOBAL],
	            .mod_fx_offset = unpatched->getValue(params::UNPATCHED_MOD_FX_OFFSET),
	            .mod_fx_feedback = unpatched->getValue(params::UNPATCHED_MOD_FX_FEEDBACK),
	            .eq = {.bass = unpatched->getKnob(params::UNPATCHED_BASS),
	                   .treble = unpatched->getKnob(params::UNPATCHED_TREBLE),
	                   .bass_frequency = unpatched->getKnob(params::UNPATCHED_BASS_FREQ),
	                   .treble_frequency = unpatched->getKnob(params::UNPATCHED_TREBLE_FREQ)},
	            .delay = delay,
	            .stereo = AudioEngine::renderInStereo,
	            .grain_send = halve_floor(reverbSendAmount),
	            .noise = &deluge::synth_noise,
	        },
	    .bitcrush = {.bitcrush = unpatched->getKnob(params::UNPATCHED_BITCRUSHING),
	                 .sample_rate_reduction = unpatched->getKnob(params::UNPATCHED_SAMPLE_RATE_REDUCTION)},
	    .post_fx_volume = global_param_value<params::GLOBAL_VOLUME_POST_FX>(finals),
	    .post_reverb_volume = global_param_value<params::GLOBAL_VOLUME_POST_REVERB_SEND>(finals),
	    .reverb_send = reverbSendAmount,
	    .sound = anyVoices,
	    .compressor_threshold = unpatched->getKnob(params::UNPATCHED_COMPRESSOR_THRESHOLD),
	    .stutter = stutter.stutter,
	    .stutter_rate = stutter.rate,
	};
}
