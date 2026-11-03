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
#include "dsp-next/fixed/curves/param_curve.h"
#include "memory/stack_guard.h"
#include "model/fx/chain_inputs.h"
#include "model/fx/channel_setup.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "model/model_stack.h"
#include "model/sample/sample_recorder.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "synth/matrix/descriptors.h" // kParamNeutral
#include "util/functions.h"
#include <algorithm>
#include <cstring>
#include <functional>

namespace params = deluge::modulation::params;

namespace deluge::model {

TrackChannel::TrackChannel()
    : chain_{kModFxRingAllocator, kGranularAllocator, delayArena(),
             synth::matrix::kParamNeutral[params::GLOBAL_VOLUME_POST_REVERB_SEND]} {
	initChannelSettings(binding_, view_, chain_.core(), chain_.filter());
}

TrackChannel::~TrackChannel() {
	releaseDelayMemory(chain_.core().delay());
}

[[gnu::hot]] void TrackChannel::render(ModelStackWithTimelineCounter* modelStack, ParamManager& paramManager,
                                       std::span<StereoSample> output, const ChannelBlock& block, Content content) {
	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	// Process FX and stuff. For kits, stutter happens before reverb send
	// The >>1 is to make up for the fact that we've got the preset default to effect a multiplication of 2 already (the
	// maximum multiplication would be 4)
	int32_t a = dsp::fixed::preset_combination(unpatchedParams->getValue(params::UNPATCHED_VOLUME));
	int32_t volumeAdjustment = dsp::fixed::curve_square(134217728, a) >> 1;

	int32_t reverbAmountAdjustForDrums = multiply_32x32_rshift32_rounded(block.reverb_amount_adjust, volumeAdjustment)
	                                     << 5;

	int32_t pitchAdjust =
	    dsp::fixed::curve_exponential(kMaxSampleValue, unpatchedParams->getValue(params::UNPATCHED_PITCH_ADJUST) >> 3);

	// The delay starts or stops on whether the content rendered last block.
	effects::Delay::State delay = delayWorkingState(paramManager, core().delay(), binding_.snapshot().effects.delay,
	                                                block.should_limit_delay_feedback, rendered_last_time);
	if (block.output_type == OutputType::AUDIO) {
		delay.analog_saturation = 5;
	}

	const effects::TrackChainInputs in =
	    trackChainInputs(*this, paramManager, volumeAdjustment, block.reverb_amount_adjust,
	                     block.side_chain_hit_pending, delay, block.output_type);
	effects::TrackBlock levels = chain_.begin_block(in, output.size());

	alignas(CACHE_LINE_SIZE) StereoSample channel_memory[SSI_TX_BUFFER_NUM_SAMPLES];
	memset(channel_memory, 0, sizeof(StereoSample) * output.size());
	std::span<StereoSample> channel_audio{channel_memory, output.size()};

	// Render actual Drums / AudioClip
	rendered_last_time = content({
	    .buffer = channel_audio,
	    .reverb_buffer = block.reverb_buffer,
	    .reverb_amount_adjust = reverbAmountAdjustForDrums,
	    .side_chain_hit_pending = block.side_chain_hit_pending,
	    .should_limit_delay_feedback = block.should_limit_delay_feedback,
	    .is_clip_active = block.is_clip_active,
	    .pitch_adjust = pitchAdjust,
	});

	checkStack("delay");
	// The recorder takes the block after the stutter, before pan, volume and compression, so its level is the clip's.
	SampleRecorder* recorder = block.recorder;
	auto record = [&](std::span<const StereoSample> /*block*/) {
		if (recorder != nullptr && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
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

	if (playbackHandler.isEitherClockActive() && (playbackHandler.ticksLeftInCountIn == 0) && block.is_clip_active) {
		auto& interpolating_params = paramManager.getUnpatchedParamSetSummary()->whichParamsAreInterpolating;
		const bool result = (params::kMaxNumUnpatchedParams > 32)
		                        ? (interpolating_params[0] != 0u || interpolating_params[1] != 0u)
		                        : (interpolating_params[0] != 0u);
		if (result) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(block.tick_owner, &paramManager);
			paramManager.toForTimeline()->tickSamples(output.size(), modelStackWithThreeMainThings);
		}
	}
}

ReverbCandidate TrackChannel::reverbCandidate(ReverbCandidate best, ParamManagerForTimeline& paramManager) const {
	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();
	if (unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].isAutomated(
	        *unpatchedParams->docForParam(params::UNPATCHED_REVERB_SEND_AMOUNT))
	    || !unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].containsSomething(
	        *unpatchedParams->docForParam(params::UNPATCHED_REVERB_SEND_AMOUNT), -2147483648)) {
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
	    .duck_shape = unpatchedParams->getValue(params::UNPATCHED_SIDECHAIN_SHAPE),
	    .rates = binding_.snapshot().effects.sidechain,
	};
}

} // namespace deluge::model
