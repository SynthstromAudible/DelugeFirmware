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

// The live pitch shifter's percussive back-search (dsp-next/fixed/pitch_shift/percussive_search.h) against the old hop
// end's (reference_hop_plan.h), over every shift's tuning, random moments and noise, on several histories (the old
// end compared its averages as floats, the search cross-multiplies: Q6); and the two comparisons over the search's
// range.

#include "pitch_shift_histories.h"
#include "reference_hop_plan.h"

#include "dsp-next/fixed/pitch_shift/percussive_search.h"
#include "dsp-next/fixed/pitch_shift/pitch_shift_tuning.h"

#include <cppspec.hpp>
#include <cstdint>
#include <random>

namespace {
namespace ps = deluge::dsp::fixed::pitch_shift;
namespace ph = pitch_shift_histories;
using deluge::synth::reference_hop::reference_frames_back;

template <typename Frame>
bool searches_as_the_old_hop_end(uint32_t seed) {
	std::mt19937 rng(seed);
	for (const ph::HistoryCase& hc : ph::history_cases()) {
		const auto history = ph::history_of<Frame>(hc.signal, hc.frames);
		for (int k = 0; k < 600; ++k) {
			// A shift up, 0 to 36 semitones
			const auto phase = static_cast<int32_t>((1U << 24) + (rng() % (7U << 24)));
			const ps::PitchShiftTuning::Coefficients t = ps::PitchShiftTuning::coefficients({.phase_increment = phase});
			const uint64_t now = history->frames_written() - (rng() % 128);
			deluge::dsp::fixed::Cong a(static_cast<uint32_t>(rng()));
			deluge::dsp::fixed::Cong b = a;
			const int32_t got = ps::PercussiveSearch::frames_back(t, now, *history, a);
			const int32_t want = reference_frames_back(t.min_search, t.max_search, t.random_element, now, *history, b);
			if (got != want || a.state() != b.state()) {
				return false;
			}
		}
	}
	return true;
}
} // namespace

// clang-format off
describe percussive_search("deluge::dsp::fixed::pitch_shift::PercussiveSearch", ${
	it("searches back as the old hop end did, mono and stereo", _ {
		expect(searches_as_the_old_hop_end<deluge::dsp::Q<31>>(0x31u)).to_be_true();
		expect(searches_as_the_old_hop_end<deluge::dsp::StereoSample>(0x32u)).to_be_true();
	});

	it("cross-multiplies as the old float comparison decided (quirk Q6)", _ {
		// total / n > best / m, as floats, against total * m > best * n, as integers, over the search's range: up to 64
		// entries of up to 63 each
		std::mt19937 rng(0x33u);
		bool same = true;
		for (int k = 0; k < 200000; ++k) {
			const auto n = static_cast<uint32_t>(1 + (rng() % 64));
			const auto m = static_cast<uint32_t>(1 + (rng() % 64));
			const auto total = static_cast<uint32_t>(rng() % (63 * n + 1));
			const auto best = static_cast<uint32_t>(rng() % (63 * m + 1));
			const bool as_floats = static_cast<float>(total) / static_cast<float>(n)
			                       > static_cast<float>(best) / static_cast<float>(m);
			const bool as_integers = uint64_t{total} * m > uint64_t{best} * n;
			same = same && as_floats == as_integers;
		}
		expect(same).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(percussive_search);
