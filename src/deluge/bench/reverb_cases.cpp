/// @file
/// @brief The reverb cases' rig, setup and blocks.
/// @details Each block is the song reverb's render as renderReverb makes it, through the engine's own reverb.

#include "bench/cases.h"

#include "dsp/reverb/reverb.hpp"
#include "processing/engines/audio_engine.h"

#include <algorithm>
#include <array>

namespace deluge::bench {

namespace {

/// @brief The return level renderReverb computes with no duck: (0x20000000 >> 15) * (0x20000000 >> 14).
constexpr int32_t kReturnLevel = 1 << 29;

struct ReverbRig {
	dsp::Buffer<StereoSample> work{};      ///< The lanes output's first n frames, processed in place.
	std::array<int32_t, kMaxBlock> send{}; ///< The reverb send, Q31.
	/// send's first n samples, built at setup so that the timed block builds no span (cases.h, BlockSpans).
	std::span<int32_t> send_block{};
};

ReverbRig rig{};

/// @return The engine's model for @p kind.
constexpr dsp::Reverb::Model model_of(ReverbKind kind) {
	switch (kind) {
	case ReverbKind::MUTABLE:
		return dsp::Reverb::Model::MUTABLE;
	case ReverbKind::DIGITAL:
		return dsp::Reverb::Model::DIGITAL;
	case ReverbKind::FREEVERB:
		break;
	}
	return dsp::Reverb::Model::FREEVERB;
}

} // namespace

void ReverbCase::setup(std::size_t n) {
	AudioEngine::reverb.setModel(model_of(kind_)); // a fresh model over zeroed memory, as a song load builds it
	AudioEngine::reverb.setRoomSize(30.f / 50);    // the boot song's knobs (Song::Song)
	AudioEngine::reverb.setHPF(0.f);
	AudioEngine::reverb.setLPF(1.f);
	AudioEngine::reverb.setDamping(36.f / 50);
	AudioEngine::reverb.setWidth(1.f);
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	std::ranges::copy(spans.in, rig.work.begin());
	for (std::size_t i = 0; i < n; ++i) {
		rig.send[i] = spans.in[i].l >> 1;
	}
	rig.send_block = std::span<int32_t>{rig.send.data(), n};
}

void ReverbCase::block(std::size_t /*n*/) {
	AudioEngine::reverb.setPanLevels(kReturnLevel, kReturnLevel);
	AudioEngine::reverb.process(rig.send_block, rig.work);
}

} // namespace deluge::bench
