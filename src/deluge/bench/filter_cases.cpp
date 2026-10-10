/// @file
/// @brief The filter cases' rig, setup and blocks, built with the DSP library's flags.
/// @details The filters ship from deluge_dsp_fixed, which builds in ARM mode with -finline-functions and no LTO, where
/// the app builds in Thumb mode with -fno-inline-functions. This file takes the library's flags
/// (src/deluge/CMakeLists.txt), as delay_cases.cpp does. Each block calls the filter set through its public
/// `configure` and `renderMono` or `renderStereo`, as a voice calls it, so the cases keep timing whatever filters the
/// set holds.

#include "bench/cases.h"
#include "util/waves.h" // jcong, the synth-wide noise the low-pass ladder draws from

#include <array>
#include <cstddef>

namespace deluge::bench {

namespace {

/// @brief The dry copy a filter fades against: a block's samples, two per frame in stereo.
alignas(16) std::array<q31, 2 * kMaxBlock> fade_scratch{};
/// @brief The parallel route's copy of the block.
alignas(16) std::array<q31, 2 * kMaxBlock> parallel_scratch{};

/// @brief The rig every filter case renders with, in on-chip SRAM.
FilterRig rig{};

} // namespace

const FilterRig& filter_rig() {
	return rig;
}

template <bool Stereo>
void FilterCase<Stereo>::setup(std::size_t n) {
	rig.set.reset();
	rig.gain = 0;
	jcong = kFilterNoiseSeed;
	rig.work = in_place_filter_block(n * kLanes);
}

template <bool Stereo>
void FilterCase<Stereo>::block(std::size_t /*n*/) {
	rig.gain = rig.set.configure(kFilterGain, config_.low_config, config_.low, config_.high_config, config_.high,
	                             config_.route);
	const dsp::fixed::FilterScratch scratch{.fade = fade_scratch, .parallel = parallel_scratch};
	if constexpr (Stereo) {
		rig.set.renderStereo(rig.work, scratch);
	}
	else {
		rig.set.renderMono(rig.work, scratch);
	}
}

template class FilterCase<false>;
template class FilterCase<true>;

} // namespace deluge::bench
