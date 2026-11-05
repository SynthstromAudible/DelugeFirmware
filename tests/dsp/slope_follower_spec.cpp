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

// The live input's slope follower and percussiveness (dsp-next/fixed/pitch_shift/slope_follower.h) against the
// loop the pitch shifter's input buffer ran, copied below over words: every step's slope and memory, over random and
// full-scale inputs, and the measure over the cases its integer arithmetic turns on.

#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/pitch_shift/slope_follower.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace {
using deluge::dsp::Mono;
using deluge::dsp::Q;
using deluge::dsp::fixed::pitch_shift::percussiveness;
using deluge::dsp::fixed::pitch_shift::SlopeFollower;

// The input buffer's follower over words, as it ran per sample
struct ReferenceFollower {
	int32_t last_sample_read = 0;
	int32_t last_angle = 0;
	std::array<int32_t, kDifferenceLPFPoles> angle_lpf_mem{};

	// The slope after @p this_sample_read; last_angle holds the slope before it until the end of the step
	int32_t step(int32_t this_sample_read) {
		auto angle =
		    static_cast<int32_t>(static_cast<uint32_t>(this_sample_read) - static_cast<uint32_t>(last_sample_read));
		last_sample_read = this_sample_read;
		if (angle < 0) {
			angle = static_cast<int32_t>(0U - static_cast<uint32_t>(angle));
		}
		for (int32_t& mem : angle_lpf_mem) {
			const auto distance_to_go = static_cast<int32_t>(static_cast<uint32_t>(angle) - static_cast<uint32_t>(mem));
			// The rounded high word of the distance times 2^23
			const auto step =
			    static_cast<int32_t>((int64_t{distance_to_go} * (int64_t{1} << 23) + (int64_t{1} << 31)) >> 32);
			mem = static_cast<int32_t>(static_cast<uint32_t>(mem) + static_cast<uint32_t>(step));
			angle = mem;
		}
		return angle;
	}
};

// The input buffer's measure, with tanh<23> as the library gives it
uint8_t reference_percussiveness(int32_t angle, int32_t last_angle) {
	auto difference = static_cast<int32_t>(static_cast<uint32_t>(angle) - static_cast<uint32_t>(last_angle));
	if (difference < 0) {
		difference = static_cast<int32_t>(0U - static_cast<uint32_t>(difference));
	}
	const int32_t p = angle != 0 ? static_cast<int32_t>((static_cast<uint64_t>(static_cast<int64_t>(difference))
	                                                     * 262144 / static_cast<uint64_t>(static_cast<int64_t>(angle)))
	                                                    >> 1)
	                             : 0;
	return static_cast<uint8_t>(deluge::dsp::fixed::tanh<23>(Q<31>::from_raw(p)).raw());
}

std::vector<int32_t> inputs(uint32_t seed, int32_t low, int32_t high, std::size_t n) {
	std::mt19937 rng(seed);
	std::uniform_int_distribution<int32_t> draw(low, high);
	std::vector<int32_t> v(n);
	for (auto& x : v) {
		x = draw(rng);
	}
	return v;
}
} // namespace

// clang-format off
describe slope_follower("deluge::dsp::fixed::pitch_shift::SlopeFollower", ${
	it("follows the slope as the input buffer did, sample by sample, over a quarter-scale fold", _ {
		for (const std::size_t block : {std::size_t{1}, std::size_t{7}, std::size_t{128}}) {
			const auto x = inputs(0x51u + block, -(1 << 29), (1 << 29) - 1, 20000);
			ReferenceFollower reference;
			Mono<SlopeFollower> follower;
			std::vector<Q<31>> out(x.size());
			for (std::size_t i = 0; i < x.size(); i += block) {
				const std::size_t n = std::min(block, x.size() - i);
				std::vector<Q<31>> in(n);
				for (std::size_t k = 0; k < n; k++) {
					in[k] = Q<31>::from_raw(x[i + k]);
				}
				follower.render_block(in, std::span<Q<31>>{out.data() + i, n});
			}
			bool same = true;
			for (std::size_t i = 0; i < x.size(); i++) {
				same = same && out[i].raw() == reference.step(x[i]);
			}
			expect(same).to_be_true();
			expect(follower.state().last.raw()).to_equal(reference.last_sample_read);
			expect(follower.state().poles[0].raw()).to_equal(reference.angle_lpf_mem[0]);
			expect(SlopeFollower::slope(follower.state()).raw()).to_equal(reference.angle_lpf_mem[1]);
		}
	});

	it("wraps as the input buffer did over full-scale swings, a stereo fold's span", _ {
		const auto x = inputs(0x77u, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), 20000);
		ReferenceFollower reference;
		Mono<SlopeFollower> follower;
		bool same = true;
		for (const int32_t word : x) {
			same = same && follower.render(Q<31>::from_raw(word)).raw() == reference.step(word);
		}
		expect(same).to_be_true();
	});

	it("keeps the slope one step back as the input buffer's last angle", _ {
		const auto x = inputs(0x99u, -(1 << 29), (1 << 29) - 1, 1000);
		ReferenceFollower reference;
		Mono<SlopeFollower> follower;
		for (const int32_t word : x) {
			std::ignore = follower.render(Q<31>::from_raw(word));
			reference.last_angle = reference.step(word);
			expect(SlopeFollower::slope(follower.state()).raw()).to_equal(reference.last_angle);
		}
	});

	it("measures percussiveness as the input buffer did", _ {
		std::mt19937 rng(0xABCu);
		std::uniform_int_distribution<int32_t> any(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
		std::uniform_int_distribution<int32_t> small(-4096, 4096);
		std::vector<std::pair<int32_t, int32_t>> cases{
		    {0, 0},          {0, 123},
		    {1, 0},          {std::numeric_limits<int32_t>::min(), 0},
		    {0x10000, 0},    {std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min()},
		    {-5, 7},         {1 << 20, (1 << 20) + 1},
		};
		for (int i = 0; i < 20000; i++) {
			cases.emplace_back(any(rng), any(rng));
			const int32_t slope = small(rng) << 12;
			cases.emplace_back(slope, slope + small(rng));
		}
		bool same = true;
		bool reached_nonzero = false;
		for (const auto [slope, previous] : cases) {
			const uint8_t got = percussiveness(Q<31>::from_raw(slope), Q<31>::from_raw(previous));
			same = same && got == reference_percussiveness(slope, previous);
			reached_nonzero = reached_nonzero || (got != 0 && got < 64);
		}
		expect(same).to_be_true();
		expect(reached_nonzero).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(slope_follower);
