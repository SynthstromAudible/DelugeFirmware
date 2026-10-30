/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// The bitcrush's two kernels, BitDepth and SampleRateReducer, composed as the effects' adapter composes them (the
// depth alone when the reduction is off; the reducer, masking its grabs one bit less, when on, sent `start` each time
// it switches on), against the frozen bitcrush over seeded full-scale frames cut into blocks of every size, across
// both params' range and every preset's switch-on value, the reduction switched off for one block in eight; the
// volume shift against the frozen bitcrush's lowering of the volume.

#include "dsp-next/fixed/shaping/bit_depth.h"
#include "dsp-next/fixed/shaping/sample_rate_reducer.h"

#include "dsp-next/core/mono.h"
#include "effects_workloads.h"
#include "reference_effects.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;
using namespace effects_workloads;

namespace {

static_assert(processor_kernel<BitDepth>);
static_assert(processor_kernel<SampleRateReducer>);
static_assert(!std::is_polymorphic_v<BitDepth>);
static_assert(!std::is_polymorphic_v<SampleRateReducer>);
static_assert(!BitDepth::on(-2113929217));
static_assert(BitDepth::on(-2113929216));
static_assert(BitDepth::volume_shift(std::numeric_limits<q31>::max()) == 3);
static_assert(BitDepth::volume_shift(0x40000000) == 2);
static_assert(BitDepth::volume_shift(0x20000000) == 1);
static_assert(BitDepth::volume_shift(0) == 0);
static_assert(BitDepth::volume_shift(std::numeric_limits<q31>::min()) == 0);
static_assert(BitDepth::mask(std::numeric_limits<q31>::min(), BitDepth::kMaskBase) == 0xFFFFFFFFu);
static_assert(BitDepth::mask(-2113929216, BitDepth::kMaskBase) == 0xFFFFFFFFu << 19);
static_assert(BitDepth::mask(std::numeric_limits<q31>::max(), BitDepth::kGrabMaskBase) == 0xFFFFFFFFu << 25);
static_assert(!SampleRateReducer::on(std::numeric_limits<q31>::min()));
static_assert(SampleRateReducer::on(std::numeric_limits<q31>::min() + 1));

constexpr std::size_t kFrames = 2048;

/// The adapter's composition of the two kernels.
struct Composed {
	Mono<BitDepth> depth;
	Mono<SampleRateReducer> reducer;
	bool reducing = false;

	int process(std::span<StereoSample> block, int32_t bitcrush, int32_t srr) {
		const int volume_shift = BitDepth::volume_shift(bitcrush);
		if (!SampleRateReducer::on(srr)) {
			reducing = false;
			if (BitDepth::on(bitcrush)) {
				depth.configure({.bitcrush = bitcrush});
				depth.renderBlock(block, block);
			}
			return volume_shift;
		}
		if (!reducing) {
			reducing = true;
			reducer.on<&SampleRateReducer::start>();
		}
		reducer.configure(
		    {.sample_rate_reduction = srr, .grab_mask = BitDepth::mask(bitcrush, BitDepth::kGrabMaskBase)});
		reducer.renderBlock(block, block);
		return volume_shift;
	}
};

/// Both params' sweep and every preset's switch-on value, (p << 29) − 2^31.
std::vector<int32_t> values() {
	std::vector<int32_t> v{kSweep.begin(), kSweep.end()};
	for (int64_t p = 0; p < 8; ++p) {
		v.push_back(static_cast<int32_t>((p << 29) - 2147483648LL));
	}
	v.push_back(0x47654321);
	v.push_back(-0x38765432);
	return v;
}

} // namespace

// clang-format off
describe bitcrush_kernels_spec("the bitcrush as a bit-depth kernel and a sample-rate reducer", ${
	it("equals the frozen bitcrush across both params", _ {
		const std::vector<int32_t> vs = values();
		for (const int32_t crush : vs) {
			for (const int32_t srr : vs) {
				reference::Bitcrush ref;
				Composed k;
				std::vector<StereoSample> a = frames(kFrames, 0xB17C0002);
				std::vector<StereoSample> b = a;
				std::size_t at = 0;
				for (std::size_t i = 0; at < a.size(); ++i) {
					const std::size_t n = block_at(i, a.size() - at);
					const int32_t block_srr = i % 8 == 5 ? std::numeric_limits<int32_t>::min() : srr;
					int32_t volume = 1 << 27;
					ref.process(std::span{a}.subspan(at, n), {.bitcrush = crush, .sample_rate_reduction = block_srr},
					            volume);
					const int shift = k.process(std::span{b}.subspan(at, n), crush, block_srr);
					expect(volume).to_equal((1 << 27) >> shift);
					at += n;
				}
				expect(same(a, b)).to_be_true();
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(bitcrush_kernels_spec);
