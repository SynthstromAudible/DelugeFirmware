/// @file
/// @brief The stutter cases' rig, setup and blocks.
/// @details Each block is one call into the stutter as its owner (ModControllableAudio::processStutter) makes it, over
/// a tape begun as the gold knob's press begins one, with the stutter-rate param in a bench param manager.

#include "bench/cases.h"

#include "definitions_cxx.hpp"
#include "model/fx/stutterer.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <tuple>

namespace deluge::bench {

namespace {

namespace params = deluge::modulation::params;

/// @brief What every stutter case renders with: the param manager holding the stutter rate, and the work block. The
/// cases run one after another, so they share it; each case's setup begins a stutter anew.
struct StutterRig {
	ParamManagerForTimeline params;        ///< The owner's params: the stutter rate.
	bool ready = false;                    ///< The param manager is set up.
	dsp::Buffer<StereoSample> work{};      ///< The lanes output's first n frames, processed in place.
	std::span<const StereoSample> input{}; ///< The lanes input's first n frames, the sounding block.
};

StutterRig rig{};

/// @brief Sets the rig's stutter-rate param to @p value, as a knob turn leaves it.
void set_rate(int32_t value) {
	UnpatchedParamSet* unpatched = rig.params.getUnpatchedParamSet();
	unpatched->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(
	    *unpatched->docForParam(params::UNPATCHED_STUTTER_RATE), value);
}

/// @brief One block as the owner renders it.
void render_block() {
	stutterer.processStutter(rig.work, &rig.params, kStutterMagnitude, kStutterTickInverse);
}

} // namespace

void StutterCase::setup(std::size_t n) {
	if (!rig.ready) {
		rig.ready = rig.params.setupUnpatched() == Error::NONE;
	}
	stutterer.endStutter(nullptr);
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	rig.input = spans.in;
	// A recording case begins at the slowest rate, whose tape outlasts the warm-up and timed blocks; a playing case
	// begins fast and records its short tape untimed.
	set_rate(config_.play ? kStutterRateFast : kStutterRateSlowest);
	std::ignore = stutterer.beginStutter(&rig, &rig.params, deluge::effects::StutterState{.quantized = false},
	                                     kStutterMagnitude, kStutterTickInverse);
	if (config_.play) {
		for (std::size_t frames = 0; frames < kStutterRecordFrames; frames += n) {
			std::ranges::copy(rig.input, rig.work.begin());
			render_block();
		}
	}
	// A resampled case moves the rate off the tape's native one.
	if (config_.resampled) {
		set_rate((config_.play ? kStutterRateFast : kStutterRateSlowest) + kStutterRateNudge);
	}
	std::ranges::copy(rig.input, rig.work.begin());
}

void StutterCase::block(std::size_t /*n*/) {
	render_block();
}

} // namespace deluge::bench
