/// @file
/// @brief The delay whole cases' rig, setup and blocks, built with the DSP library's flags.
/// @details The delay ships from deluge_dsp_fixed, which builds in ARM mode with -finline-functions and no LTO, where
/// the app builds in Thumb mode with -fno-inline-functions. This file takes the library's flags
/// (src/deluge/CMakeLists.txt), as delay_feedback_cases.cpp does, so each case's block is one call into the shipped
/// `DelayProcessor::process`, made as the delay's owner makes it.

#include "bench/cases.h"
#include "board_config.h" // PLACE_SDRAM_BSS
#include "dsp-next/fixed/noise/noise.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::bench {

namespace {

using dsp::fixed::DelayProcessor;
using dsp::fixed::DelayTape;

/// @brief The seed the tapes' noise starts from.
constexpr uint32_t kTapeSeed = 0x7A9E'5EED;

// The tapes' storage goes in SDRAM on the device, where the app allocates its delay tapes (allocLowSpeed). A host
// build of the spec has no SDRAM section to put it in, and a named section there would store its zeroes in the binary.
#if defined(DELUGE_DSP_BENCH)
#define DELAY_TAPE_STORAGE PLACE_SDRAM_BSS
#else
#define DELAY_TAPE_STORAGE
#endif

/// @brief The primary tape's storage.
DELAY_TAPE_STORAGE std::array<StereoSample, kDelayStorageSize> primary_storage;
/// @brief The secondary tape's storage.
DELAY_TAPE_STORAGE std::array<StereoSample, kDelayStorageSize> secondary_storage;

/// @brief The rig every delay whole case renders with, in on-chip SRAM.
DelayRig rig{};

/// @brief A tape for a delay at @p rate over the start of @p storage, sized as the app sizes it (effects::tape_shape):
/// the ideal size for the rate, kWriteLag samples more for the write's run-off.
[[nodiscard]] DelayTape tape_for_rate(std::span<StereoSample> storage, int32_t rate) {
	const auto [size, clamped] = DelayTape::ideal_size_for_rate(static_cast<uint32_t>(rate));
	const std::size_t samples = static_cast<std::size_t>(size) + static_cast<std::size_t>(DelayTape::kWriteLag);
	DelayTape tape{storage.first(samples), static_cast<std::size_t>(size), static_cast<uint32_t>(rate)};
	if (clamped) {
		tape.make_native_rate_precise();
	}
	return tape;
}

} // namespace

void fill_delay_tape(std::span<StereoSample> storage) {
	dsp::fixed::SeededNoise noise{kTapeSeed};
	for (StereoSample& sample : storage) {
		sample.l = noise.render() >> 10;
		sample.r = noise.render() >> 10;
	}
}

const DelayRig& delay_rig() {
	return rig;
}

void DelayWholeCase::setup(std::size_t n) {
	rig.processor = DelayProcessor{};
	rig.primary_storage = primary_storage;
	rig.secondary_storage = secondary_storage;

	const std::size_t primary_samples = DelayTape::kNeutralSize + static_cast<std::size_t>(DelayTape::kWriteLag);
	fill_delay_tape(rig.primary_storage.first(primary_samples));
	rig.primary = tape_for_rate(rig.primary_storage, kDelayNeutralRate);

	rig.secondary = DelayTape{};
	rig.growing = nullptr;
	if (config_.growing) {
		// The owner's settled-rate growth (Delay::initializeSecondaryBuffer): a tape native at the new rate, the
		// primary's native rate made precise relative to it.
		std::ranges::fill(rig.secondary_storage, StereoSample{.l = 0, .r = 0});
		rig.secondary = tape_for_rate(rig.secondary_storage, kDelayResHiRate);
		rig.primary.make_native_rate_precise_relative_to(rig.secondary);
		rig.growing = &rig.secondary;
	}

	rig.settings = delay_whole_block(config_);
	rig.result = {};

	fill_delay_input();
	const FrameSpans<StereoSample> spans = stereo_frame_spans(n);
	rig.work = spans.out;
	std::ranges::transform(spans.in, rig.work.begin(),
	                       [](StereoSample frame) { return StereoSample{.l = frame.l >> 1, .r = frame.r >> 1}; });
}

void DelayWholeCase::block(std::size_t /*n*/) {
	rig.result = rig.processor.process(rig.work, &rig.primary, rig.growing, rig.settings);
}

} // namespace deluge::bench
