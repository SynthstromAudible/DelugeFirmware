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

#include "model/fx/chain_inputs.h"

#include "model/fx/song_stutter.h"
#include "model/global_effectable/global_effectable_for_clip.h"
#include "model/global_effectable/global_effectable_for_song.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_scratch.h"
#include "util/functions.h"
#include "util/param_functions.h"

#include <limits>

namespace params = deluge::modulation::params;

namespace deluge::model {

effects::FilterParams chainFilterParams(UnpatchedParamSet& unpatched) {
	const int32_t lpfFrequencyParam = unpatched.getValue(params::UNPATCHED_LPF_FREQ);
	const int32_t lpfMorphParam = unpatched.getValue(params::UNPATCHED_LPF_MORPH);
	const int32_t hpfFrequencyParam = unpatched.getValue(params::UNPATCHED_HPF_FREQ);
	const int32_t hpfMorphParam = unpatched.getValue(params::UNPATCHED_HPF_MORPH);
	return {
	    .lpf_frequency = getFinalParameterValueExp(paramNeutralValues[params::LOCAL_LPF_FREQ],
	                                               cableToExpParamShortcut(lpfFrequencyParam)),
	    .lpf_resonance =
	        getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_LPF_RESONANCE],
	                                     cableToLinearParamShortcut(unpatched.getValue(params::UNPATCHED_LPF_RES))),
	    .lpf_morph = getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_LPF_MORPH],
	                                              cableToExpParamShortcut(lpfMorphParam)),
	    .hpf_frequency = getFinalParameterValueExp(paramNeutralValues[params::LOCAL_HPF_FREQ],
	                                               cableToExpParamShortcut(hpfFrequencyParam)),
	    .hpf_resonance =
	        getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_HPF_RESONANCE],
	                                     cableToLinearParamShortcut(unpatched.getValue(params::UNPATCHED_HPF_RES))),
	    .hpf_morph = getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_HPF_MORPH],
	                                              cableToExpParamShortcut(hpfMorphParam)),
	    .lpf_frequency_param = lpfFrequencyParam,
	    .lpf_morph_param = lpfMorphParam,
	    .hpf_frequency_param = hpfFrequencyParam,
	    .hpf_morph_param = hpfMorphParam,
	    .allow_oversampling = AudioEngine::cpuDireness < 14,
	};
}

effects::FxInputs hostFxInputs(GlobalEffectable& owner, ParamManager& paramManager, const effects::Delay::State& delay,
                               int32_t grainSend) {
	UnpatchedParamSet* unpatched = paramManager.getUnpatchedParamSet();
	return {
	    .settings = &owner.modControllableSnapshot().effects,
	    .mod_fx_type = owner.getActiveModFXType(&paramManager),
	    .mod_fx_rate =
	        getFinalParameterValueExp(paramNeutralValues[params::GLOBAL_MOD_FX_RATE],
	                                  cableToExpParamShortcut(unpatched->getValue(params::UNPATCHED_MOD_FX_RATE))),
	    .mod_fx_depth = getFinalParameterValueVolume(
	        paramNeutralValues[params::GLOBAL_MOD_FX_DEPTH],
	        cableToLinearParamShortcut(unpatched->getValue(params::UNPATCHED_MOD_FX_DEPTH))),
	    .mod_fx_offset = unpatched->getValue(params::UNPATCHED_MOD_FX_OFFSET),
	    .mod_fx_feedback = unpatched->getValue(params::UNPATCHED_MOD_FX_FEEDBACK),
	    .eq = {.bass = unpatched->getValue(params::UNPATCHED_BASS),
	           .treble = unpatched->getValue(params::UNPATCHED_TREBLE),
	           .bass_frequency = unpatched->getValue(params::UNPATCHED_BASS_FREQ),
	           .treble_frequency = unpatched->getValue(params::UNPATCHED_TREBLE_FREQ)},
	    .delay = delay,
	    .stereo = AudioEngine::renderInStereo,
	    .grain_send = grainSend,
	};
}

effects::ClipChainInputs clipChainInputs(GlobalEffectableForClip& owner, ParamManager& paramManager,
                                         int32_t volumeAdjustment, int32_t reverbAmountAdjust,
                                         int32_t sideChainHitPending, const effects::Delay::State& delay,
                                         OutputType outputType) {
	UnpatchedParamSet* unpatched = paramManager.getUnpatchedParamSet();

	// Make it a bit bigger so that default filter resonance doesn't reduce volume overall. Kits keep the fudged
	// number they first had; audio tracks the exact one.
	int32_t volumePostFX = volumeAdjustment;
	volumePostFX += (outputType == OutputType::AUDIO) ? multiply_32x32_rshift32_rounded(volumeAdjustment, 471633397)
	                                                  : (volumeAdjustment >> 2);

	const int32_t reverbSendAmount = getFinalParameterValueVolume(
	    reverbAmountAdjust, cableToLinearParamShortcut(unpatched->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT)));
	const StutterInput stutter = stutterInput(owner, &paramManager);

	return {
	    .fx = hostFxInputs(owner, paramManager, delay, reverbSendAmount),
	    .bitcrush = {.bitcrush = unpatched->getValue(params::UNPATCHED_BITCRUSHING),
	                 .sample_rate_reduction = unpatched->getValue(params::UNPATCHED_SAMPLE_RATE_REDUCTION)},
	    .filter = chainFilterParams(*unpatched),
	    .filter_scratch = render::filter_scratch(),
	    .volume_post_fx = volumePostFX,
	    .reverb_send = reverbSendAmount,
	    .pan = unpatched->getValue(params::UNPATCHED_PAN) >> 1,
	    .sidechain_on = unpatched->getValue(params::UNPATCHED_SIDECHAIN_VOLUME) != std::numeric_limits<q31_t>::min(),
	    .sidechain_hit = sideChainHitPending,
	    .sidechain_shape = unpatched->getValue(params::UNPATCHED_SIDECHAIN_SHAPE),
	    .sidechain_depth = owner.getSidechainVolumeAmountAsPatchCableDepth(&paramManager),
	    .tick_inverse = playbackHandler.getTimePerInternalTickInverse(),
	    .post_reverb_volume_neutral = paramNeutralValues[params::GLOBAL_VOLUME_POST_REVERB_SEND],
	    .compressor_threshold = unpatched->getValue(params::UNPATCHED_COMPRESSOR_THRESHOLD),
	    .stutter = stutter.stutter,
	    .stutter_rate = stutter.rate,
	};
}

effects::MasterChainInputs masterChainInputs(Song& song, int32_t volumePostFX) {
	GlobalEffectableForSong& master = song.live.global_effectable;
	ParamManager& paramManager = song.paramManager;
	const effects::Delay::State delay = master.createDelayWorkingState(paramManager);
	const int32_t reverbSendAmount =
	    getFinalParameterValueVolume(paramNeutralValues[params::GLOBAL_REVERB_AMOUNT],
	                                 cableToLinearParamShortcut(paramManager.getUnpatchedParamSet()->getValue(
	                                     params::UNPATCHED_REVERB_SEND_AMOUNT)));
	return {
	    .fx = hostFxInputs(master, paramManager, delay, reverbSendAmount >> 3),
	    .volume_post_fx = volumePostFX,
	    .post_reverb_volume = paramNeutralValues[params::GLOBAL_VOLUME_POST_REVERB_SEND],
	    .reverb_send = reverbSendAmount >> 1,
	};
}

effects::MasterPostInputs masterPostInputs(Song& song) {
	GlobalEffectableForSong& master = song.live.global_effectable;
	UnpatchedParamSet* unpatched = song.paramManager.getUnpatchedParamSet();
	const StutterInput stutter = stutterInput(master, &song.paramManager);

	// The pan applies after the reverb, in stereo only.
	const int32_t pan = unpatched->getValue(params::UNPATCHED_PAN) >> 1;
	int32_t amplitudeL = 0;
	int32_t amplitudeR = 0;
	const bool doPanning = pan != 0 && AudioEngine::renderInStereo && shouldDoPanning(pan, &amplitudeL, &amplitudeR);

	return {
	    .filter = chainFilterParams(*unpatched),
	    .filter_scratch = render::filter_scratch(),
	    .bitcrush = {.bitcrush = unpatched->getValue(params::UNPATCHED_BITCRUSHING),
	                 .sample_rate_reduction = unpatched->getValue(params::UNPATCHED_SAMPLE_RATE_REDUCTION)},
	    // 134217728 made a bit bigger, so the filters' default resonance doesn't reduce the volume overall.
	    .master_volume = 167763968,
	    .do_pan = doPanning,
	    .pan_l = amplitudeL,
	    .pan_r = amplitudeR,
	    .song_volume = getFinalParameterValueVolume(
	                       134217728, cableToLinearParamShortcut(unpatched->getValue(params::UNPATCHED_VOLUME)))
	                   >> 1,
	    .stutter = stutter.stutter,
	    .stutter_rate = stutter.rate,
	};
}

} // namespace deluge::model
