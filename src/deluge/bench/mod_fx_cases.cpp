/// @file
/// @brief The mod FX cases' rig, setup and blocks.
/// @details Each block is one call into `effects::ModFx::render`, made as its owner (ModControllableAudio::processFX)
/// makes it, over a ring taken through the component's prepare, commit and finish as the owner's type change takes one.

#include "bench/cases.h"

#include "definitions_cxx.hpp"
#include "engines/effects/mod_fx.h"
#include "model/mod_controllable/mod_fx_allocator.h"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace deluge::bench {

namespace {

/// @brief What every mod FX case renders with: one component and the work block. The cases run one after another,
/// so they share it; each case's setup makes the component anew.
struct ModFxRig {
	std::optional<effects::ModFx> mod_fx; ///< The component, as a Sound's.
	int32_t volume{ONE_Q31 >> 1};         ///< The post-effect volume the component scales in place (Q31).
	dsp::Buffer<StereoSample> work{};     ///< The lanes output's first n frames, processed in place.
};

ModFxRig rig{};

/// @return The app's effect type for @p kind.
constexpr ModFXType mod_fx_type(ModFxKind kind) {
	switch (kind) {
	case ModFxKind::CHORUS:
		return ModFXType::CHORUS;
	case ModFxKind::CHORUS_STEREO:
		return ModFXType::CHORUS_STEREO;
	case ModFxKind::DIMENSION:
		return ModFXType::DIMENSION;
	case ModFxKind::FLANGER:
		return ModFXType::FLANGER;
	case ModFxKind::PHASER:
		return ModFXType::PHASER;
	case ModFxKind::WARBLE:
		return ModFXType::WARBLE;
	}
	return ModFXType::NONE;
}

} // namespace

void ModFxCase::setup(std::size_t n) {
	// A fresh component, so each case starts from the same state: its write index and LFOs at rest (an owner never
	// resets them), and a zeroed ring for the effects that run over one.
	const ModFXType type = mod_fx_type(config_.kind);
	rig.mod_fx.reset();
	rig.mod_fx.emplace(model::kModFxRingAllocator);
	effects::RingChange ring = rig.mod_fx->prepare(type);
	rig.mod_fx->commit(type, ring);
	rig.mod_fx->finish(ring);
	rig.volume = ONE_Q31 >> 1;
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	std::ranges::copy(spans.in, rig.work.begin());
}

void ModFxCase::block(std::size_t /*n*/) {
	const effects::ModFxInputs in{
	    .active = mod_fx_type(config_.kind),
	    .rate = kModFxRate,
	    .depth = kModFxDepth,
	    .offset = kModFxParam,
	    .feedback = kModFxParam,
	    .stereo = config_.stereo,
	    .sound = true,
	};
	rig.mod_fx->render(rig.work, in, rig.volume);
}

} // namespace deluge::bench
