/// @file
/// @brief The stutter cases' rig, setup and blocks.
/// @details Each block is one call into the stutter as an owner's chain makes it (deluge::model::stutterInput): the
/// rate from the stutter-rate param in a bench param manager, then the song's stutter component rendering over a tape
/// begun as the gold knob's press begins one.

#include "bench/cases.h"

#include "definitions_cxx.hpp"
#include "effects/stutter.h"
#include "model/fx/song_stutter.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "util/functions.h"

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

/// The stutter the cases render: the component the song's stutter holds, over the same allocator.
effects::Stutter fx{model::kStutterAllocator};

/// @brief Sets the rig's stutter-rate param to @p value, as a knob turn leaves it.
void set_rate(int32_t value) {
	UnpatchedParamSet* unpatched = rig.params.getUnpatchedParamSet();
	unpatched->params[params::UNPATCHED_STUTTER_RATE].setCurrentValueBasicForSetup(
	    *unpatched->docForParam(params::UNPATCHED_STUTTER_RATE), value);
}

/// @brief One block as the owner renders it.
void render_block() {
	const auto rate = static_cast<uint32_t>(model::stutterRate(&rig.params, 0, kStutterMagnitude, kStutterTickInverse));
	fx.render(rig.work, rate);
}

} // namespace

void StutterCase::setup(std::size_t n) {
	if (!rig.ready) {
		rig.ready = rig.params.setupUnpatched() == Error::NONE;
	}
	effects::StutterChange ended{};
	fx.commit_end(ended);
	fx.finish(ended);
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	rig.input = spans.in;
	// A recording case begins at a rate whose tape outlasts the warm-up and timed blocks; a playing case begins at the
	// knob's middle and records its short tape untimed.
	set_rate(config_.play ? kStutterRatePlay : kStutterRateRecord);
	effects::StutterChange began =
	    fx.prepare(static_cast<uint32_t>(model::stutterRate(&rig.params, 0, kStutterMagnitude, kStutterTickInverse)));
	std::ignore = fx.commit(began, effects::StutterState{.quantized = false});
	fx.finish(began);
	if (config_.play) {
		for (std::size_t frames = 0; frames < kStutterRecordFrames; frames += n) {
			std::ranges::copy(rig.input, rig.work.begin());
			render_block();
		}
	}
	// A resampled case moves the rate off the tape's native one.
	if (config_.resampled) {
		set_rate((config_.play ? kStutterRatePlay : kStutterRateRecord) + kStutterRateNudge);
	}
	std::ranges::copy(rig.input, rig.work.begin());
}

void StutterCase::block(std::size_t /*n*/) {
	render_block();
}

} // namespace deluge::bench
