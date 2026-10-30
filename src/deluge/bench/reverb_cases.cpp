/// @file
/// @brief The reverb cases' rig, setup and blocks.
/// @details Each block is the song reverb's render as renderReverb makes it, through the engine's reverb component.

#include "bench/cases.h"

#include "engines/effects/reverb.h"
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
	std::span<const int32_t> send_block{};
};

ReverbRig rig{};

/// @return The engine's model for @p kind.
constexpr effects::ReverbModel model_of(ReverbKind kind) {
	switch (kind) {
	case ReverbKind::MUTABLE:
		return effects::ReverbModel::MUTABLE;
	case ReverbKind::DIGITAL:
		return effects::ReverbModel::DIGITAL;
	case ReverbKind::FREEVERB:
		break;
	}
	return effects::ReverbModel::FREEVERB;
}

} // namespace

void ReverbCase::setup(std::size_t n) {
	// A fresh model over zeroed memory, as a song load builds it, at the boot song's knobs (Song::Song). The bench
	// runs with IRQs masked, so it needs no excluded section.
	AudioEngine::reverb.begin_change();
	AudioEngine::reverb.prepare(model_of(kind_));
	AudioEngine::reverb.commit();
	AudioEngine::reverb.publish({.room_size = 30.f / 50, .damping = 36.f / 50, .width = 1.f, .hpf = 0.f, .lpf = 1.f});
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	std::ranges::copy(spans.in, rig.work.begin());
	for (std::size_t i = 0; i < n; ++i) {
		rig.send[i] = spans.in[i].l >> 1;
	}
	rig.send_block = std::span<const int32_t>{rig.send.data(), n};
}

void ReverbCase::block(std::size_t /*n*/) {
	AudioEngine::reverb.render(rig.work, rig.send_block, kReturnLevel, kReturnLevel);
}

} // namespace deluge::bench
