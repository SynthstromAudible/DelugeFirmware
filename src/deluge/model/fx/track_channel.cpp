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

#include "model/fx/track_channel.h"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include "memory/stack_guard.h"
#include "model/fx/chain_inputs.h"
#include "model/fx/channel_setup.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "model/model_stack.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "synth/matrix/param_format.h" // param_neutral
#include "util/functions.h"
#include <algorithm>
#include <cstring>
#include <functional>

namespace params = deluge::modulation::params;

namespace deluge::model {

TrackChannel::TrackChannel()
    : chain_{kModFxRingAllocator, kGranularAllocator, delayArena(),
             synth::matrix::param_neutral<params::GLOBAL_VOLUME_POST_REVERB_SEND>()} {
	initChannelSettings(binding_, view_, chain_.core(), chain_.filter());
}

TrackChannel::~TrackChannel() {
	releaseDelayMemory(chain_.core().delay());
}

[[gnu::hot]] void TrackChannel::render(ModelStackWithTimelineCounter* modelStack, ParamManager& paramManager,
                                       std::span<StereoSample> output, const ChannelBlock& block, Content content) {
	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	// Process FX and stuff. For kits, stutter happens before reverb send
	const dsp::Q<27> volumeAdjustment =
	    conversions_.volume(unpatchedParams->getKnob(params::UNPATCHED_VOLUME), volume_from_knob);

	// The song's Q27 reverb adjust times the channel's Q27 volume adjustment, rounded to Q22, moved to Q27
	int32_t reverbAmountAdjustForDrums =
	    mul_r_as_wrap<dsp::Q<27>>(dsp::Q<27>::from_raw(block.reverb_amount_adjust), volumeAdjustment).raw();

	const dsp::Q<24> pitchAdjust =
	    conversions_.pitch_adjust(unpatchedParams->getKnob(params::UNPATCHED_PITCH_ADJUST), pitch_adjust_from_knob);

	alignas(CACHE_LINE_SIZE) StereoSample channel_memory[SSI_TX_BUFFER_NUM_SAMPLES];
	memset(channel_memory, 0, sizeof(StereoSample) * output.size());
	std::span<StereoSample> channel_audio{channel_memory, output.size()};

	// Render actual Drums / AudioClip. Nothing in the chain feeds it, so it comes first, to know whether the chain can
	// be skipped: the content's own render skipping makes it cheap when there's nothing to play.
	const bool rendered = content({
	    .buffer = channel_audio,
	    .reverb_buffer = block.reverb_buffer,
	    .reverb_amount_adjust = reverbAmountAdjustForDrums,
	    .side_chain_hit_pending = block.side_chain_hit_pending,
	    .should_limit_delay_feedback = block.should_limit_delay_feedback,
	    .is_clip_active = block.is_clip_active,
	    .pitch_adjust = pitchAdjust,
	});

	SampleRecorder* recorder = block.recorder;
	const bool recording =
	    recorder != nullptr && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING;
	// A stutter replays what the chain held, so it brings the chain back even over silent content
	const bool stuttering = currentSong != nullptr && currentSong->live.stutter.isStuttering(core());
	if (skipping_) {
		if (!rendered && !recording && !stuttering) {
			// Still nothing to process: the chain stays off, and only the params move on
			rendered_last_time = false;
			core().compressor().dsp().reset();
			tickParamAutomation(modelStack, paramManager, output.size(), block);
			return;
		}
		stopSkipping(paramManager);
	}

	// The delay takes in sound from this block's render
	rendered_last_time = rendered;
	effects::Delay::State delay =
	    delayWorkingState(paramManager, core().delay(), binding_.snapshot().effects.delay,
	                      block.should_limit_delay_feedback, rendered_last_time, conversions_);
	if (block.output_type == OutputType::AUDIO) {
		delay.analog_saturation = 5;
	}

	const effects::TrackChainInputs in =
	    trackChainInputs(*this, paramManager, volumeAdjustment, block.reverb_amount_adjust,
	                     block.side_chain_hit_pending, delay, block.output_type, conversions_);
	effects::TrackBlock levels = chain_.begin_block(in, output.size());

	checkStack("delay");
	// The recorder takes the block after the stutter, before pan, volume and compression, so its level is the clip's.
	auto record = [&](std::span<const StereoSample> /*block*/) {
		if (recording) {
			// Self-overdubs compensate for sample playback's 1/8 scaling. Standalone sampling retains its existing
			// gain.
			recorder->feedAudio(channel_audio, true, recorder->recordingOwnClip ? 3 : 2);
		}
	};
	if (chain_.process(channel_audio, in, levels, rendered_last_time, {block.reverb_buffer, output.size()}, record)) {
		AudioEngine::timeThereWasLastSomeReverb = AudioEngine::audioSampleTimer;
	}

	// Add the channel's audio to the output
	std::ranges::transform(channel_audio, output, output.begin(), std::plus{});

	tickParamAutomation(modelStack, paramManager, output.size(), block);

	reassessSkipping(paramManager, recording || stuttering);
}

void TrackChannel::tickParamAutomation(ModelStackWithTimelineCounter* modelStack, ParamManager& paramManager,
                                       size_t frames, const ChannelBlock& block) {
	if (playbackHandler.isEitherClockActive() && (playbackHandler.ticksLeftInCountIn == 0) && block.is_clip_active) {
		auto& interpolating_params = paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating;
		const bool result = (params::kMaxNumUnpatchedParams > 32)
		                        ? (interpolating_params[0] != 0u || interpolating_params[1] != 0u)
		                        : (interpolating_params[0] != 0u);
		if (result) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(block.tick_owner, &paramManager);
			paramManager.toForTimeline()->tickSamples(frames, modelStackWithThreeMainThings);
		}
	}
}

int32_t TrackChannel::samplesUntilTailsEnd(ParamManager& paramManager) {
	const ModFXType modFXType = activeModFXType(binding_.snapshot(), paramManager);
	if (modFXType == ModFXType::NONE && core().compressor().dsp().threshold() <= dsp::Q<31>{}) {
		return 0;
	}
	int32_t modFXSamples = 0;
	switch (modFXType) {
	case ModFXType::NONE:
		break;
	case ModFXType::CHORUS:
		[[fallthrough]];
	case ModFXType::CHORUS_STEREO:
		modFXSamples = 20 * 44;
		break;
	case ModFXType::GRAIN:
		modFXSamples = core().granular().samplesToShutdown();
		break;
	default:
		modFXSamples = 90 * 441;
	}
	return std::max(modFXSamples, static_cast<int32_t>(core().compressor().dsp().release_ms() * 44));
}

void TrackChannel::reassessSkipping(ParamManager& paramManager, bool held) {
	const bool silent = !rendered_last_time && core().delay().repeatsUntilAbandon() == 0u && !held;
	if (!silent) {
		start_skipping_at_ = 0;
		return;
	}

	const int32_t waitSamples = samplesUntilTailsEnd(paramManager);
	if (waitSamples == 0) {
		start_skipping_at_ = 0;
		startSkipping();
		return;
	}

	// If the wait for the tails hasn't started, start it; if it has, see whether it is over
	if (start_skipping_at_ == 0) {
		start_skipping_at_ = AudioEngine::audioSampleTimer + waitSamples;
	}
	else if (static_cast<int32_t>(AudioEngine::audioSampleTimer - start_skipping_at_) >= 0) {
		start_skipping_at_ = 0;
		startSkipping();
	}
}

void TrackChannel::startSkipping() {
	time_started_skipping_ = AudioEngine::audioSampleTimer;
	skipping_ = true;
	core().granular().startSkippingRendering();
}

void TrackChannel::stopSkipping(ParamManager& paramManager) {
	// Catch up what would have moved on meanwhile, as a sound coming back from skipping does
	const uint32_t timeOff = AudioEngine::audioSampleTimer - time_started_skipping_;
	if (timeOff != 0u) {
		const int32_t modFXRate = effects::final_value(
		    effects::ModFx::kParams[0], paramManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_MOD_FX_RATE));
		core().mod_fx().tick(static_cast<int32_t>(timeOff), modFXRate);

		if (AudioEngine::sizeLastSideChainHit != 0) {
			core().sidechain().registerHitRetrospectively(
			    AudioEngine::sizeLastSideChainHit, AudioEngine::audioSampleTimer - AudioEngine::timeLastSideChainHit,
			    binding_.snapshot().effects.sidechain, playbackHandler.getTimePerInternalTickInverse());
		}
		// The post-reverb volume's last value is from before skipping: don't ramp from it
		core().send_and_volume().forget();
	}
	start_skipping_at_ = 0;
	skipping_ = false;
}

ReverbCandidate TrackChannel::reverbCandidate(ReverbCandidate best, ParamManagerForTimeline& paramManager) const {
	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();
	if (unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].isAutomated(
	        unpatchedParams->findDocForParam(params::UNPATCHED_REVERB_SEND_AMOUNT))
	    || !unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].containsSomething(
	        unpatchedParams->findDocForParam(params::UNPATCHED_REVERB_SEND_AMOUNT), -2147483648)) {
		return best;
	}
	const int32_t reverbHere = unpatchedParams->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT);
	if (!beats(reverbHere, best)) {
		return best;
	}
	return {
	    .amount = reverbHere,
	    .found = true,
	    .duck_depth = sidechainDepth(paramManager),
	    .duck_shape = unpatchedParams->getKnob(params::UNPATCHED_SIDECHAIN_SHAPE),
	    .rates = binding_.snapshot().effects.sidechain,
	};
}

} // namespace deluge::model
