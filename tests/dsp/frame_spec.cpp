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

// Frame<T, N>: N samples, one per lane. Its layout is N T's in lane order, so a block of frames is N interleaved
// streams; Frame<q31, 2> is StereoSample's layout, and the two convert both ways without changing a bit.

#include "dsp-next/core/frame.h"

#include "dsp-next/core/stereo_sample.h"

#include <array>
#include <bit>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

using namespace deluge::dsp;

namespace {

static_assert(std::is_trivially_copyable_v<Frame<q31, 1>>);
static_assert(std::is_trivially_copyable_v<Frame<q31, 8>>);
static_assert(std::is_standard_layout_v<Frame<q31, 3>>);
static_assert(std::is_aggregate_v<Frame<q31, 4>>);
static_assert(sizeof(Frame<q31, 1>) == sizeof(q31));
static_assert(sizeof(Frame<q31, 3>) == 3 * sizeof(q31));
static_assert(sizeof(Frame<q31, 8>) == 8 * sizeof(q31));
static_assert(alignof(Frame<q31, 8>) == alignof(q31));
static_assert(sizeof(Frame<q31, 2>) == sizeof(StereoSample));
static_assert(alignof(Frame<q31, 2>) == alignof(StereoSample));

/// A block of three 4-lane frames is twelve q31 in frame order, lane order within each frame.
static_assert(sizeof(std::array<Frame<q31, 4>, 3>) == 12 * sizeof(q31));

constexpr q31 kMin = std::numeric_limits<q31>::min();
constexpr q31 kMax = std::numeric_limits<q31>::max();

/// The frame round trip, at compile time.
static_assert(to_frame(StereoSample{.l = 5, .r = -7})[0] == 5);
static_assert(to_frame(StereoSample{.l = 5, .r = -7})[1] == -7);
static_assert(to_stereo(Frame<q31, 2>{{kMin, kMax}}).l == kMin);
static_assert(to_stereo(Frame<q31, 2>{{kMin, kMax}}).r == kMax);

} // namespace

// clang-format off
describe frame_spec("Frame", ${
	it("indexes its lanes in order, for reading and for writing", _ {
		Frame<q31, 4> f{{10, -20, 30, -40}};
		expect(f[0]).to_equal(q31{10});
		expect(f[1]).to_equal(q31{-20});
		expect(f[2]).to_equal(q31{30});
		expect(f[3]).to_equal(q31{-40});

		f[2] = kMin;
		expect(f.lane[2]).to_equal(kMin);
		expect(f[1]).to_equal(q31{-20});
		expect(f[3]).to_equal(q31{-40});

		const Frame<q31, 4>& view = f;
		expect(view[2]).to_equal(kMin);
	});

	it("lays out a block of frames as N interleaved streams", _ {
		std::array<Frame<q31, 3>, 2> block{{{{1, 2, 3}}, {{4, 5, 6}}}};
		const auto flat = std::bit_cast<std::array<q31, 6>>(block);
		for (std::size_t i = 0; i < 6; ++i) {
			expect(flat[i]).to_equal(static_cast<q31>(i + 1));
		}
	});

	it("takes a stereo frame's left to lane 0 and its right to lane 1, and back, at the extremes", _ {
		for (const q31 l : {kMin, q31{-1}, q31{0}, q31{1}, kMax}) {
			for (const q31 r : {kMin, q31{-1}, q31{0}, q31{1}, kMax}) {
				const StereoSample s{.l = l, .r = r};
				const Frame<q31, 2> f = to_frame(s);
				expect(f[0]).to_equal(l);
				expect(f[1]).to_equal(r);
				const StereoSample back = to_stereo(f);
				expect(back.l).to_equal(l);
				expect(back.r).to_equal(r);
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(frame_spec);
