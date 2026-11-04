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

/// A block of three 4-lane frames is twelve samples in frame order, lane order within each frame.
static_assert(sizeof(std::array<Frame<q31, 4>, 3>) == 12 * sizeof(q31));

constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();

/// The frame round trip, at compile time.
static_assert(to_frame(StereoSample{.l = Q<31>::from_raw(5), .r = Q<31>::from_raw(-7)})[0].raw() == 5);
static_assert(to_frame(StereoSample{.l = Q<31>::from_raw(5), .r = Q<31>::from_raw(-7)})[1].raw() == -7);
static_assert(to_stereo(Frame<q31, 2>{{q31::min(), q31::max()}}).l.raw() == kMin);
static_assert(to_stereo(Frame<q31, 2>{{q31::min(), q31::max()}}).r.raw() == kMax);

} // namespace

// clang-format off
describe frame_spec("Frame", ${
	it("indexes its lanes in order, for reading and for writing", _ {
		Frame<q31, 4> f{{q31::from_raw(10), q31::from_raw(-20), q31::from_raw(30), q31::from_raw(-40)}};
		expect(f[0].raw()).to_equal(10);
		expect(f[1].raw()).to_equal(-20);
		expect(f[2].raw()).to_equal(30);
		expect(f[3].raw()).to_equal(-40);

		f[2] = q31::from_raw(kMin);
		expect(f.lane[2].raw()).to_equal(kMin);
		expect(f[1].raw()).to_equal(-20);
		expect(f[3].raw()).to_equal(-40);

		const Frame<q31, 4>& view = f;
		expect(view[2].raw()).to_equal(kMin);
	});

	it("lays out a block of frames as N interleaved streams", _ {
		std::array<Frame<q31, 3>, 2> block{};
		for (std::size_t i = 0; i < 6; ++i) {
			block[i / 3][i % 3] = q31::from_raw(static_cast<int32_t>(i + 1));
		}
		const auto flat = std::bit_cast<std::array<int32_t, 6>>(block);
		for (std::size_t i = 0; i < 6; ++i) {
			expect(flat[i]).to_equal(static_cast<int32_t>(i + 1));
		}
	});

	it("takes a stereo frame's left to lane 0 and its right to lane 1, and back, at the extremes", _ {
		for (const int32_t l : {kMin, int32_t{-1}, int32_t{0}, int32_t{1}, kMax}) {
			for (const int32_t r : {kMin, int32_t{-1}, int32_t{0}, int32_t{1}, kMax}) {
				const StereoSample s{.l = Q<31>::from_raw(l), .r = Q<31>::from_raw(r)};
				const Frame<q31, 2> f = to_frame(s);
				expect(f[0].raw()).to_equal(l);
				expect(f[1].raw()).to_equal(r);
				const StereoSample back = to_stereo(f);
				expect(back.l.raw()).to_equal(l);
				expect(back.r.raw()).to_equal(r);
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(frame_spec);
