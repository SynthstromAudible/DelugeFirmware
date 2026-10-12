/// @file
/// @brief The granular cases' rig, setup and blocks.
/// @details Each block is one call into `GranularProcessor::processGrainFX`, made as its owner
/// (ModControllableAudio::processFX) makes it, with the reverb feed going to the audio engine's buffer.

#include "bench/cases.h"

#include "definitions_cxx.hpp"
#include "dsp/granular/GranularProcessor.h"
#include "util/waves.h"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace deluge::bench {

namespace {

/// @brief What every granular case renders with: one processor and the work block. The cases run one after another,
/// so they share it; each case's setup makes the processor anew.
struct GranularRig {
	std::optional<GranularProcessor> processor; ///< The processor, as a Sound's.
	int32_t volume{ONE_Q31 >> 1};               ///< The post-effect volume the processor scales (Q31).
	dsp::Buffer<StereoSample> work{};           ///< The lanes output's first n frames, processed in place.
	std::span<const StereoSample> input{};      ///< The lanes input's first n frames, the sounding block.
};

GranularRig rig{};

/// @brief The noise generator's state at the start of every case, so that each case and run draws the same grains.
constexpr uint32_t kNoiseSeed = 380116160;

/// @brief One call as the owner makes it: a fresh post-effect volume and the reverb amount derived from it.
/// @param config The case's knobs.
void render_block(const GranularConfig& config) {
	rig.volume = ONE_Q31 >> 1;
	const int32_t reverb_amount = multiply_32x32_rshift32(rig.volume, kGranularVerbAmount) << 5;
	rig.processor->processGrainFX(rig.work, kGranularRate, kGranularMix, config.density, config.randomness, &rig.volume,
	                              true, kGranularTempoBpm, reverb_amount);
}

} // namespace

void GranularCase::setup(std::size_t n) {
	// A fresh processor, so each case starts from the same state; the destroyed one releases its ring.
	rig.processor.reset();
	rig.processor.emplace();
	jcong = kNoiseSeed;
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	rig.input = spans.in;
	// The untimed blocks take the ring's allocation and run it past one full ring, so that grains are live and
	// initialised from the first timed block.
	for (std::size_t frames = 0; frames < kGranularFillFrames; frames += n) {
		std::ranges::copy(rig.input, rig.work.begin());
		render_block(config_);
	}
	std::ranges::copy(rig.input, rig.work.begin());
}

void GranularCase::block(std::size_t /*n*/) {
	render_block(config_);
}

} // namespace deluge::bench
