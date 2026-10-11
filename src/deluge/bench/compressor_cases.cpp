/// @file
/// @brief The compressor cases' rig, setup and blocks, built with the DSP library's flags.
/// @details The compressor ships from deluge_dsp_fixed, which builds in ARM mode with -finline-functions and no LTO,
/// where the app builds in Thumb mode with -fno-inline-functions. This file takes the library's flags
/// (src/deluge/CMakeLists.txt), as filter_cases.cpp does. Each block is one call into the shipped
/// `RmsFeedbackCompressor::render` or `renderVolumeNeutral`, made as the compressor's owners make it.

#include "bench/cases.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace deluge::bench {

namespace {

/// @brief The scratch a blended block is copied to: the caller's, as deluge::render::compressor_scratch() is.
alignas(16) std::array<StereoSample, kMaxBlock> scratch{};

/// @brief The rig every compressor case renders with, in on-chip SRAM.
CompressorRig rig{};

} // namespace

const CompressorRig& compressor_rig() {
	return rig;
}

void CompressorCase::setup(std::size_t n) {
	rig.compressor = dsp::fixed::RmsFeedbackCompressor{};
	rig.compressor.configure(compressor_knobs(config_));
	fill_compressor_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	std::ranges::copy(spans.in, rig.work.begin());
}

void CompressorCase::block(std::size_t /*n*/) {
	if (config_.neutral) {
		rig.compressor.renderVolumeNeutral(rig.work, kCompressorFinalVolume, scratch);
	}
	else {
		rig.compressor.render(rig.work, kCompressorVolume, kCompressorVolume, kCompressorFinalVolume >> 3, scratch);
	}
}

} // namespace deluge::bench
