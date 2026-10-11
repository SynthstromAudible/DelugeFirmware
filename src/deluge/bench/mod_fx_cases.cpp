/// @file
/// @brief The mod FX cases' rig, setup and blocks, built with the app's flags.
/// @details The mod FX ships in the app (`ModFXProcessor`), which builds in Thumb mode with -fno-inline-functions, so
/// this file takes the app's flags and times the code as the app runs it. Each block is one call into
/// `ModFXProcessor::processModFX`, made as its owner makes it.

#include "bench/cases.h"

#include "definitions_cxx.hpp"
#include "model/mod_controllable/ModFXProcessor.h"
#include "modulation/params/param_collection_summary.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"

#include <algorithm>
#include <cstddef>

namespace deluge::bench {

namespace {

/// @brief What every mod FX case renders with: one processor, the unpatched params it reads and the work block. The
/// cases run one after another, so they share it; each case's setup resets it.
struct ModFxRig {
	ModFxRig() : params(&summary) {
		summary.paramCollection = &params;
		params.params[deluge::modulation::params::UNPATCHED_MOD_FX_OFFSET].setCurrentValueBasicForSetup(kModFxParam);
		params.params[deluge::modulation::params::UNPATCHED_MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(kModFxParam);
	}

	ModFXProcessor processor;         ///< The processor, as a Sound's.
	ParamCollectionSummary summary{}; ///< The summary the params are built on.
	UnpatchedParamSet params;         ///< The unpatched params, with the offset and feedback set.
	int32_t volume{ONE_Q31 >> 1};     ///< The post-effect volume the processor scales in place (Q31).
	dsp::Buffer<StereoSample> work{}; ///< The lanes output's first n frames, processed in place.
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
	// The phaser runs on the processor's allpass memory; the delay-line effects on its buffer.
	if (config_.kind == ModFxKind::PHASER) {
		rig.processor.disableBuffer();
	}
	else {
		rig.processor.setupBuffer();
	}
	rig.processor.resetMemory();
	rig.processor.modFXBufferWriteIndex = 0;
	rig.processor.modFXLFO = LFO{};
	rig.processor.modFXLFOStereo = LFO{};
	rig.volume = ONE_Q31 >> 1;
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	std::ranges::copy(spans.in, rig.work.begin());
}

void ModFxCase::block(std::size_t /*n*/) {
	const bool was_stereo = AudioEngine::renderInStereo;
	AudioEngine::renderInStereo = config_.stereo;
	rig.processor.processModFX(rig.work, mod_fx_type(config_.kind), kModFxRate, kModFxDepth, &rig.volume, &rig.params,
	                           true);
	AudioEngine::renderInStereo = was_stereo;
}

} // namespace deluge::bench
