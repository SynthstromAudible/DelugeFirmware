/// @file
/// @brief The granular cases' rig, setup and blocks.
/// @details Each block is one call into `effects::Granular::render`, made as an owner's chain
/// (effects::ChainCore::render_fx) makes it, over a State and ring taken through the component's prepare,
/// commit and finish as a Sound's change to GRAIN takes them, with the reverb feed going to the rig's send buffer.

#include "bench/cases.h"

#include "definitions_cxx.hpp"
#include "engines/effects/granular.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "util/waves.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>

namespace deluge::bench {

namespace {

/// @brief What every granular case renders with: one component, the work block and the reverb send. The cases run one
/// after another, so they share it; each case's setup makes the component anew.
struct GranularRig {
	std::optional<effects::Granular> granular; ///< The component, as a Sound's.
	int32_t volume{ONE_Q31 >> 1};              ///< The post-effect volume the component scales (Q31).
	dsp::Buffer<StereoSample> work{};          ///< The lanes output's first n frames, processed in place.
	std::span<const StereoSample> input{};     ///< The lanes input's first n frames, the sounding block.
	std::array<q31, kMaxBlock> send{};         ///< The reverb send the component adds its feed into, Q31.
};

GranularRig rig{};

/// @brief The noise generator's state at the start of every case, so that each case and run draws the same grains.
constexpr uint32_t kNoiseSeed = 380116160;

/// @brief One call as the owner makes it: a fresh post-effect volume and the reverb amount derived from it.
/// @param config The case's knobs.
void render_block(const GranularConfig& config) {
	rig.volume = ONE_Q31 >> 1;
	const effects::GranularInputs in{
	    .rate = kGranularRate,
	    .mix = kGranularMix,
	    .density = config.density,
	    .randomness = config.randomness,
	    .send_amount = multiply_32x32_rshift32(rig.volume, kGranularVerbAmount) << 5,
	    .sound = true,
	};
	rig.granular->render(rig.work, in, rig.volume, std::span{rig.send}.first(rig.work.size()));
}

} // namespace

void GranularCase::setup(std::size_t n) {
	// A fresh component, so each case starts from the same state; the destroyed one releases its State and ring.
	rig.granular.reset();
	rig.granular.emplace(model::kGranularAllocator, effects::EnterGrain::CLEAR);
	effects::GranularBlockChange change = rig.granular->prepare(ModFXType::GRAIN);
	rig.granular->commit(ModFXType::GRAIN, change);
	rig.granular->finish(change);
	jcong = kNoiseSeed;
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	rig.input = spans.in;
	// The untimed blocks run the ring past one full lap, so that grains are live and initialised from the first timed
	// block.
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
