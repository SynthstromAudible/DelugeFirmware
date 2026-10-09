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

// ShiftTracker: y += (x - y) >> N, returning y (TRACK) or x - y (REMOVE). Each mode is pinned against the expression
// its user runs: REMOVE at N = 11 against the delay's DC blocker, TRACK at N = 7 against the low-pass ladder's noise
// follower. Both take the difference after the update. Then Mono<ShiftTracker> against a hand loop over the kernel.

#include "dsp-next/fixed/filter/shift_tracker.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/noise/noise.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

namespace {

using Blocker = ShiftTracker<11>;
using Follower = ShiftTracker<7>;

static_assert(processor_kernel<Blocker>);
static_assert(moded_kernel<Blocker>);
static_assert(!std::is_polymorphic_v<Blocker>);
static_assert(processor<Mono<Blocker>>);
static_assert(Blocker::coefficients({.mode = Blocker::Mode::REMOVE}).mode == Blocker::Mode::REMOVE);
static_assert(Blocker::coefficients({}).mode == Blocker::Mode::TRACK);

/// The samples the long comparisons run.
constexpr std::size_t kLongRun = 10'000;
/// The seed of every noise input.
constexpr uint32_t kSeed = 0x0DC0'B10C;

/// @p n samples of seeded noise at half scale, so that x - y, for y tracking x, stays inside q31.
std::vector<q31> half_scale_noise(std::size_t n, uint32_t seed) {
	SeededNoise noise{seed};
	std::vector<q31> v(n);
	for (q31& x : v) {
		x = noise.render() >> 1;
	}
	return v;
}

/// The block lengths the wrapper comparisons run, ending on a long odd one.
constexpr std::array<std::size_t, 6> kLengths{1, 2, 3, 7, 64, 129};

} // namespace

describe shift_tracker_spec(
    "ShiftTracker", $ {
	    it(
	        "takes the difference after the update", _ {
		        constexpr q31 x = q31{1} << 20;
		        Blocker::State track{};
		        expect(Blocker::step<Blocker::Mode::TRACK>({}, {}, track, x)).to_equal(q31{512});
		        expect(track.y).to_equal(q31{512});

		        Blocker::State remove{};
		        expect(Blocker::step<Blocker::Mode::REMOVE>({.mode = Blocker::Mode::REMOVE}, {}, remove, x))
		            .to_equal(x - q31{512});
		        expect(remove.y).to_equal(q31{512});
	        });

	    it(
	        "removes the slow part at N = 11 exactly as the delay's DC blocker does", _ {
		        const std::vector<q31> in = half_scale_noise(kLongRun, kSeed);
		        const Blocker::Coefficients c = Blocker::coefficients({.mode = Blocker::Mode::REMOVE});
		        Blocker::State s{};
		        q31 hp = 0;
		        for (const q31 sample : in) {
			        q31 x = sample;
			        hp += (x - hp) >> 11;
			        x -= hp;
			        expect(Blocker::step<Blocker::Mode::REMOVE>(c, {}, s, sample)).to_equal(x);
		        }
		        expect(s.y).to_equal(hp);
		        expect(hp).not_().to_equal(q31{0});
	        });

	    it(
	        "tracks at N = 7 exactly as the low-pass ladder's noise follower does", _ {
		        SeededNoise noise{kSeed};
		        const Follower::Coefficients c = Follower::coefficients({.mode = Follower::Mode::TRACK});
		        Follower::State s{};
		        q31 noise_last = 0;
		        for (std::size_t i = 0; i < kLongRun; ++i) {
			        const q31 drawn = noise.render() >> 2;
			        const q31 distance_to_go = drawn - noise_last;
			        noise_last += distance_to_go >> 7;
			        expect(Follower::step<Follower::Mode::TRACK>(c, {}, s, drawn)).to_equal(noise_last);
		        }
		        expect(s.y).to_equal(noise_last);
		        expect(noise_last).not_().to_equal(q31{0});
	        });

	    it(
	        "runs in Mono as a hand loop over the kernel, its state carried from block to block", _ {
		        for (const Blocker::Mode mode : {Blocker::Mode::TRACK, Blocker::Mode::REMOVE}) {
			        Mono<Blocker> mono{{.mode = mode}};
			        const Blocker::Coefficients c = Blocker::coefficients({.mode = mode});
			        Blocker::State s{};
			        uint32_t seed = kSeed;
			        for (const std::size_t n : kLengths) {
				        const std::vector<q31> in = half_scale_noise(n, seed++);
				        std::vector<q31> got(n);
				        mono.renderBlock(in, got);
				        for (std::size_t i = 0; i < n; ++i) {
					        const q31 want = mode == Blocker::Mode::TRACK
					                             ? Blocker::step<Blocker::Mode::TRACK>(c, {}, s, in[i])
					                             : Blocker::step<Blocker::Mode::REMOVE>(c, {}, s, in[i]);
					        expect(got[i]).to_equal(want);
				        }
				        expect(mono.state().y).to_equal(s.y);
			        }
		        }
	        });
    });

CPPSPEC_MAIN(shift_tracker_spec);
