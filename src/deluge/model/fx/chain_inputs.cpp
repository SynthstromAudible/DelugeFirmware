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
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include "dsp-next/fixed/vca/pan.h"
#include "synth/matrix/param_format.h" // param_neutral
#include "util/synth_noise.h"

#include "effects/reverb.h"
#include "model/fx/master_channel.h"
#include "model/fx/song_stutter.h"
#include "model/fx/track_channel.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_scratch.h"
#include "util/functions.h"

#include <limits>

namespace params = deluge::modulation::params;

namespace deluge::model {

effects::FilterParams chainFilterParams(UnpatchedParamSet& unpatched) {
	return effects::Filter::params_from(
	    {
	        .lpf_frequency = unpatched.getValue(params::UNPATCHED_LPF_FREQ),
	        .lpf_resonance = unpatched.getValue(params::UNPATCHED_LPF_RES),
	        .lpf_morph = unpatched.getValue(params::UNPATCHED_LPF_MORPH),
	        .hpf_frequency = unpatched.getValue(params::UNPATCHED_HPF_FREQ),
	        .hpf_resonance = unpatched.getValue(params::UNPATCHED_HPF_RES),
	        .hpf_morph = unpatched.getValue(params::UNPATCHED_HPF_MORPH),
	    },
	    AudioEngine::cpuDireness < 14);
}

ModFXType activeModFXType(const snapshot::ModControllableSnapshot& snapshot, ParamManager& paramManager) {
	ModFXType modFXTypeNow = snapshot.effects.mod_fx.type;

	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	if ((snapshot.mod_fx_param == ModFXParam::DEPTH
	     && unpatchedParams->getValue(params::UNPATCHED_MOD_FX_DEPTH) == -2147483648)
	    || (snapshot.mod_fx_param == ModFXParam::FEEDBACK
	        && unpatchedParams->getValue(params::UNPATCHED_MOD_FX_FEEDBACK) == -2147483648)
	    || (snapshot.mod_fx_param == ModFXParam::OFFSET
	        && unpatchedParams->getValue(params::UNPATCHED_MOD_FX_OFFSET) == -2147483648)) {
		modFXTypeNow = ModFXType::NONE;
	}
	return modFXTypeNow;
}

effects::Delay::State delayWorkingState(ParamManager& paramManager, effects::Delay& delay,
                                        const effects::DelayState& settings, bool shouldLimitDelayFeedback,
                                        bool soundComingIn) {
	effects::Delay::State delayWorkingState;
	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	delayWorkingState.delayFeedbackAmount =
	    effects::final_value(effects::Delay::kFeedback, unpatchedParams->getValue(params::UNPATCHED_DELAY_AMOUNT));
	if (shouldLimitDelayFeedback) {
		delayWorkingState.delayFeedbackAmount =
		    std::min(delayWorkingState.delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState.userDelayRate =
	    effects::final_value(effects::Delay::kRate, unpatchedParams->getValue(params::UNPATCHED_DELAY_RATE));
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse(true);
	delay.setupWorkingState(delayWorkingState, settings, timePerTickInverse, soundComingIn);

	return delayWorkingState;
}

dsp::Q<29> sidechainDepth(ParamManager& paramManager) {
	const dsp::Q<31> knob = paramManager.getUnpatchedParamSet()->getKnob(params::UNPATCHED_SIDECHAIN_VOLUME);
	return add_wrap(as_floor<dsp::Q<29>>(knob), dsp::Q<29>::one());
}

effects::FxInputs hostFxInputs(const snapshot::ModControllableSnapshot& snapshot, ModFXType activeType,
                               ParamManager& paramManager, const effects::Delay::State& delay, dsp::Q<31> grainSend) {
	UnpatchedParamSet* unpatched = paramManager.getUnpatchedParamSet();
	const effects::ModFx::Params modFx = effects::ModFx::params_from({
	    .rate = unpatched->getValue(params::UNPATCHED_MOD_FX_RATE),
	    .depth = unpatched->getValue(params::UNPATCHED_MOD_FX_DEPTH),
	    .offset = unpatched->getValue(params::UNPATCHED_MOD_FX_OFFSET),
	    .feedback = unpatched->getValue(params::UNPATCHED_MOD_FX_FEEDBACK),
	});
	return {
	    .settings = &snapshot.effects,
	    .mod_fx_type = activeType,
	    .mod_fx_rate = modFx.rate,
	    .mod_fx_depth = modFx.depth,
	    .mod_fx_offset = modFx.offset,
	    .mod_fx_feedback = modFx.feedback,
	    .eq = {.bass = unpatched->getKnob(params::UNPATCHED_BASS),
	           .treble = unpatched->getKnob(params::UNPATCHED_TREBLE),
	           .bass_frequency = unpatched->getKnob(params::UNPATCHED_BASS_FREQ),
	           .treble_frequency = unpatched->getKnob(params::UNPATCHED_TREBLE_FREQ)},
	    .delay = delay,
	    .stereo = AudioEngine::renderInStereo,
	    .grain_send = grainSend,
	    .noise = &synth_noise,
	};
}

effects::TrackChainInputs trackChainInputs(const TrackChannel& channel, ParamManager& paramManager,
                                           dsp::Q<27> volumeAdjustment, int32_t reverbAmountAdjust,
                                           int32_t sideChainHitPending, const effects::Delay::State& delay,
                                           OutputType outputType) {
	UnpatchedParamSet* unpatched = paramManager.getUnpatchedParamSet();

	// Make it a bit bigger so that default filter resonance doesn't reduce volume overall. Kits add a quarter of the
	// volume; audio tracks add the exact factor 471633397 / 2^32: the rounded high word of the volume times the
	// Q31 constant is that product in the volume's units.
	const dsp::Q<27> headroom =
	    (outputType == OutputType::AUDIO)
	        ? dsp::reinterpret_bits<dsp::Q<27>>(mul_r(volumeAdjustment, dsp::q<31>(471633397 / 0x1p31)))
	        : scale_shr_floor<2>(volumeAdjustment);
	const dsp::Q<27> volumePostFX = add_wrap(volumeAdjustment, headroom);

	// The send's final value is Q31
	const auto reverbSendAmount = dsp::Q<31>::from_raw(effects::final_value(
	    effects::Reverb::kSend, unpatched->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT), reverbAmountAdjust));
	const StutterInput stutter = stutterInput(channel.core(), &paramManager);
	const snapshot::ModControllableSnapshot& snapshot = channel.binding().snapshot();

	return {
	    .fx = hostFxInputs(snapshot, activeModFXType(snapshot, paramManager), paramManager, delay, reverbSendAmount),
	    .bitcrush = {.bitcrush = unpatched->getKnob(params::UNPATCHED_BITCRUSHING),
	                 .sample_rate_reduction = unpatched->getKnob(params::UNPATCHED_SAMPLE_RATE_REDUCTION)},
	    .filter = chainFilterParams(*unpatched),
	    .filter_scratch = render::filter_scratch(),
	    .volume_post_fx = volumePostFX,
	    .reverb_send = reverbSendAmount,
	    .pan = halve_floor(unpatched->getKnob(params::UNPATCHED_PAN)),
	    .sidechain_on = unpatched->getValue(params::UNPATCHED_SIDECHAIN_VOLUME) != std::numeric_limits<int32_t>::min(),
	    .sidechain_hit = dsp::Q<31>::from_raw(sideChainHitPending), // the engine's pending hit word
	    .sidechain_shape = unpatched->getKnob(params::UNPATCHED_SIDECHAIN_SHAPE),
	    .sidechain_depth = sidechainDepth(paramManager),
	    .tick_inverse = playbackHandler.getTimePerInternalTickInverse(),
	    .post_reverb_volume_neutral = synth::matrix::param_neutral<params::GLOBAL_VOLUME_POST_REVERB_SEND>(),
	    .compressor_threshold = unpatched->getKnob(params::UNPATCHED_COMPRESSOR_THRESHOLD),
	    .stutter = stutter.stutter,
	    .stutter_rate = stutter.rate,
	};
}

effects::MasterChainInputs masterChainInputs(Song& song, dsp::Q<27> volumePostFX) {
	MasterChannel& master = song.live.master_channel;
	ParamManager& paramManager = song.paramManager;
	const snapshot::ModControllableSnapshot& snapshot = master.binding().snapshot();
	const effects::Delay::State delay =
	    delayWorkingState(paramManager, master.core().delay(), snapshot.effects.delay, false, true);
	// The send's final value is Q31
	const auto reverbSendAmount = dsp::Q<31>::from_raw(effects::final_value(
	    effects::Reverb::kSend, paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT)));
	return {
	    .fx = hostFxInputs(snapshot, activeModFXType(snapshot, paramManager), paramManager, delay,
	                       scale_shr_floor<3>(reverbSendAmount)),
	    .volume_post_fx = volumePostFX,
	    .post_reverb_volume = synth::matrix::param_neutral<params::GLOBAL_VOLUME_POST_REVERB_SEND>(),
	    .reverb_send = halve_floor(reverbSendAmount),
	};
}

effects::MasterPostInputs masterPostInputs(Song& song) {
	UnpatchedParamSet* unpatched = song.paramManager.getUnpatchedParamSet();
	const StutterInput stutter = stutterInput(song.live.master_channel.core(), &song.paramManager);

	// The pan applies after the reverb, in stereo only.
	const dsp::Q<31> pan = halve_floor(unpatched->getKnob(params::UNPATCHED_PAN));
	dsp::fixed::PanAmplitudes amplitudes{};
	const bool doPanning = pan != dsp::Q<31>{} && AudioEngine::renderInStereo;
	if (doPanning) {
		amplitudes = dsp::fixed::pan_amplitudes(pan);
	}

	return {
	    .filter = chainFilterParams(*unpatched),
	    .filter_scratch = render::filter_scratch(),
	    .noise = &synth_noise,
	    .bitcrush = {.bitcrush = unpatched->getKnob(params::UNPATCHED_BITCRUSHING),
	                 .sample_rate_reduction = unpatched->getKnob(params::UNPATCHED_SAMPLE_RATE_REDUCTION)},
	    .master_volume = effects::kMasterVolume,
	    .do_pan = doPanning,
	    .pan_l = amplitudes.left,
	    .pan_r = amplitudes.right,
	    .song_volume = volume_from_knob(unpatched->getKnob(params::UNPATCHED_VOLUME)),
	    .stutter = stutter.stutter,
	    .stutter_rate = stutter.rate,
	};
}

} // namespace deluge::model
