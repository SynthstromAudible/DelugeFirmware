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
#include "dsp/core/fixed_point.h"
#include "dsp/fixed/curves/param_curve.h"
#include "dsp/fixed/vca/pan.h"
#include "model/fx/effect_reads.h"
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

effects::FilterParams chainFilterParams(UnpatchedParamSet& unpatched, ChainConversions& conversions) {
	return conversions.filter(
	    {
	        .words = filterWords(unpatched, host::chains::kFilterIds),
	        .allow_oversampling = AudioEngine::cpuDireness < 14,
	    },
	    [](const FilterWordsAndOversampling& in) {
		    return effects::Filter::params_from(in.words, in.allow_oversampling);
	    });
}

ModFXType activeModFXType(const EffectsBinding& binding, ParamManager& paramManager) {
	ModFXType modFXTypeNow = binding.renderEffects().mod_fx.type;

	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	if ((binding.renderModFXParam() == ModFXParam::DEPTH
	     && unpatchedParams->getValue(unpatchedIndex(host::chains::param::kModFXDepth)) == -2147483648)
	    || (binding.renderModFXParam() == ModFXParam::FEEDBACK
	        && unpatchedParams->getValue(unpatchedIndex(host::chains::param::kModFXFeedback)) == -2147483648)
	    || (binding.renderModFXParam() == ModFXParam::OFFSET
	        && unpatchedParams->getValue(unpatchedIndex(host::chains::param::kModFXOffset)) == -2147483648)) {
		modFXTypeNow = ModFXType::NONE;
	}
	return modFXTypeNow;
}

effects::Delay::State delayWorkingState(ParamManager& paramManager, effects::Delay& delay,
                                        const effects::DelayState& settings, bool shouldLimitDelayFeedback,
                                        bool soundComingIn, ChainConversions& conversions) {
	effects::Delay::State delayWorkingState;
	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	delayWorkingState.delayFeedbackAmount =
	    conversions.delay_feedback(unpatchedParams->getValue(unpatchedIndex(host::chains::param::kDelayFeedback)),
	                               [](int32_t word) { return effects::final_value(effects::Delay::kFeedback, word); });
	if (shouldLimitDelayFeedback) {
		delayWorkingState.delayFeedbackAmount =
		    std::min(delayWorkingState.delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState.userDelayRate =
	    conversions.delay_rate(unpatchedParams->getValue(unpatchedIndex(host::chains::param::kDelayRate)),
	                           [](int32_t word) { return effects::final_value(effects::Delay::kRate, word); });
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse(true);
	delay.setupWorkingState(delayWorkingState, settings, timePerTickInverse, soundComingIn);

	return delayWorkingState;
}

dsp::Q<29> sidechainDepth(ParamManager& paramManager) {
	const dsp::Q<31> knob =
	    paramManager.getUnpatchedParamSet()->getKnob(unpatchedIndex(host::chains::param::kSidechainCompressorVolume));
	return add_wrap(as_floor<dsp::Q<29>>(knob), dsp::Q<29>::one());
}

effects::FxInputs hostFxInputs(const EffectsBinding& binding, ModFXType activeType, ParamManager& paramManager,
                               const effects::Delay::State& delay, dsp::Q<31> grainSend,
                               ChainConversions& conversions) {
	UnpatchedParamSet* unpatched = paramManager.getUnpatchedParamSet();
	const effects::ModFx::Params modFx =
	    conversions.mod_fx(modFxWords(*unpatched, host::chains::kModFxIds),
	                       [](const effects::ModFx::Words& words) { return effects::ModFx::params_from(words); });
	return {
	    .settings = &binding.renderEffects(),
	    .mod_fx_type = activeType,
	    .mod_fx_rate = modFx.rate,
	    .mod_fx_depth = modFx.depth,
	    .mod_fx_offset = modFx.offset,
	    .mod_fx_feedback = modFx.feedback,
	    .eq = eqKnobs(*unpatched, host::chains::kEqIds),
	    .delay = delay,
	    .stereo = AudioEngine::renderInStereo,
	    .grain_send = grainSend,
	    .noise = &synth_noise,
	};
}

effects::TrackChainInputs trackChainInputs(const TrackChannel& channel, ParamManager& paramManager,
                                           dsp::Q<27> volumeAdjustment, int32_t reverbAmountAdjust,
                                           int32_t sideChainHitPending, const effects::Delay::State& delay,
                                           OutputType outputType, ChainConversions& conversions) {
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
	    effects::Reverb::kSend, unpatched->getValue(unpatchedIndex(host::chains::param::kReverbAmount)),
	    reverbAmountAdjust));
	const StutterInput stutter = stutterInput(channel.core(), &paramManager);
	const EffectsBinding& binding = channel.binding();

	return {
	    .fx = hostFxInputs(binding, activeModFXType(binding, paramManager), paramManager, delay, reverbSendAmount,
	                       conversions),
	    .bitcrush = bitcrushKnobs(*unpatched, host::chains::kBitcrushIds),
	    .filter = chainFilterParams(*unpatched, conversions),
	    .filter_scratch = render::filter_scratch(),
	    .volume_post_fx = volumePostFX,
	    .reverb_send = reverbSendAmount,
	    .pan = halve_floor(unpatched->getKnob(unpatchedIndex(host::chains::param::kPan))),
	    .sidechain_on = unpatched->getValue(unpatchedIndex(host::chains::param::kSidechainCompressorVolume))
	                    != std::numeric_limits<int32_t>::min(),
	    .sidechain_hit = dsp::Q<31>::from_raw(sideChainHitPending), // the engine's pending hit word
	    .sidechain_shape = unpatched->getKnob(unpatchedIndex(host::chains::param::kCompressorShape)),
	    .sidechain_depth = sidechainDepth(paramManager),
	    .tick_inverse = playbackHandler.getTimePerInternalTickInverse(),
	    .post_reverb_volume_neutral = synth::matrix::param_neutral<params::GLOBAL_VOLUME_POST_REVERB_SEND>(),
	    .compressor_threshold = unpatched->getKnob(unpatchedIndex(host::chains::param::kCompressorThreshold)),
	    .stutter = stutter.stutter,
	    .stutter_rate = stutter.rate,
	};
}

effects::MasterChainInputs masterChainInputs(Song& song, dsp::Q<27> volumePostFX) {
	MasterChannel& master = song.live.master_channel;
	ParamManager& paramManager = song.paramManager;
	const EffectsBinding& binding = master.binding();
	const effects::Delay::State delay = delayWorkingState(
	    paramManager, master.core().delay(), binding.renderEffects().delay, false, true, master.conversions());
	// The send's final value is Q31
	const auto reverbSendAmount = dsp::Q<31>::from_raw(effects::final_value(
	    effects::Reverb::kSend,
	    paramManager.getUnpatchedParamSet()->getValue(unpatchedIndex(host::chains::param::kReverbAmount))));
	return {
	    .fx = hostFxInputs(binding, activeModFXType(binding, paramManager), paramManager, delay,
	                       scale_shr_floor<3>(reverbSendAmount), master.conversions()),
	    .volume_post_fx = volumePostFX,
	    .post_reverb_volume = synth::matrix::param_neutral<params::GLOBAL_VOLUME_POST_REVERB_SEND>(),
	    .reverb_send = halve_floor(reverbSendAmount),
	};
}

effects::MasterPostInputs masterPostInputs(Song& song) {
	UnpatchedParamSet* unpatched = song.paramManager.getUnpatchedParamSet();
	MasterChannel& master = song.live.master_channel;
	const StutterInput stutter = stutterInput(master.core(), &song.paramManager);

	// The pan applies after the reverb, in stereo only.
	const dsp::Q<31> pan = halve_floor(unpatched->getKnob(unpatchedIndex(host::chains::param::kPan)));
	dsp::fixed::PanAmplitudes amplitudes{};
	const bool doPanning = pan != dsp::Q<31>{} && AudioEngine::renderInStereo;
	if (doPanning) {
		amplitudes = dsp::fixed::pan_amplitudes(pan);
	}

	return {
	    .filter = chainFilterParams(*unpatched, master.conversions()),
	    .filter_scratch = render::filter_scratch(),
	    .noise = &synth_noise,
	    .bitcrush = bitcrushKnobs(*unpatched, host::chains::kBitcrushIds),
	    .master_volume = effects::kMasterVolume,
	    .do_pan = doPanning,
	    .pan_l = amplitudes.left,
	    .pan_r = amplitudes.right,
	    .song_volume = master.conversions().volume(unpatched->getKnob(unpatchedIndex(host::chains::param::kVolume)),
	                                               volume_from_knob),
	    .stutter = stutter.stutter,
	    .stutter_rate = stutter.rate,
	};
}

} // namespace deluge::model
