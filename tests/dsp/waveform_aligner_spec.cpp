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

// The live pitch shifter's waveform aligner (dsp-next/fixed/pitch_shift/waveform_aligner.h) against the old hop end's
// alignment (reference_hop_plan.h): the offset and sub-sample phase over random windows, lengths, extents and
// directions, both ways first and backward only (quirk Q10), at shifted rates and none, on histories over several
// laps and under one (quirk Q4), mono and stereo.

#include "pitch_shift_histories.h"
#include "reference_hop_plan.h"

#include "dsp-next/fixed/pitch_shift/waveform_aligner.h"

#include <cppspec.hpp>
#include <cstdint>
#include <random>

namespace {
namespace ps = deluge::dsp::fixed::pitch_shift;
namespace ph = pitch_shift_histories;
using deluge::synth::reference_hop::reference_align;

template <typename Frame>
bool aligns_as_the_old_hop_end(uint32_t seed, std::size_t& moved, std::size_t& placed) {
	std::mt19937 rng(seed);
	for (const ph::HistoryCase& hc : ph::history_cases()) {
		const auto history = ph::history_of<Frame>(hc.signal, hc.frames);
		const uint64_t latest = history->frames_written();
		for (int k = 0; k < 600; ++k) {
			const auto length = static_cast<int32_t>(1 + (rng() % 70));
			const auto start_offset = static_cast<int32_t>(rng() % 200) - 50;
			const bool forward = (rng() & 1) != 0;
			const auto start = static_cast<uint32_t>(latest - (rng() % 2000) - static_cast<uint32_t>(3 * length))
			                   & (ps::kHistoryFrames - 1);
			const auto size = static_cast<int32_t>(rng() % 1000);
			const bool shifted = (rng() % 8) != 0;
			const auto old_start = static_cast<uint32_t>(latest - (rng() % 3000) - static_cast<uint32_t>(3 * length));
			const ps::MovingAverages old_totals = history->moving_averages(ps::RingPosition{old_start}, length);
			const ps::WaveformAligner::Search search{.start = start,
			                                         .start_offset = start_offset,
			                                         .length = length,
			                                         .size = size,
			                                         .direction = forward ? 1 : -1,
			                                         .directions_searched = forward ? 0 : 1,
			                                         .sub_sample = shifted};
			const ps::Alignment got = ps::WaveformAligner::align(*history, search, old_totals, latest);
			const auto want =
			    reference_align(*history, static_cast<int32_t>(start), start_offset, length, size, search.direction,
			                    search.directions_searched, shifted ? 25137421 : (1 << 24), old_totals, latest);
			if (got.offset != want.best_offset || got.phase != want.additional_osc_pos) {
				return false;
			}
			moved += got.offset != 0 ? 1 : 0;
			placed += got.phase != 0 ? 1 : 0;
		}
	}
	return true;
}
} // namespace

// clang-format off
describe waveform_aligner("deluge::dsp::fixed::pitch_shift::WaveformAligner", ${
	it("aligns as the old hop end did, mono", _ {
		std::size_t moved = 0;
		std::size_t placed = 0;
		expect(aligns_as_the_old_hop_end<deluge::dsp::Q<31>>(0x41u, moved, placed)).to_be_true();
		expect(moved).to_be_greater_than(std::size_t{100});
		expect(placed).to_be_greater_than(std::size_t{100});
	});

	it("aligns as the old hop end did, stereo", _ {
		std::size_t moved = 0;
		std::size_t placed = 0;
		expect(aligns_as_the_old_hop_end<deluge::dsp::StereoSample>(0x42u, moved, placed)).to_be_true();
		expect(moved).to_be_greater_than(std::size_t{100});
		expect(placed).to_be_greater_than(std::size_t{100});
	});
});
// clang-format on

CPPSPEC_MAIN(waveform_aligner);
