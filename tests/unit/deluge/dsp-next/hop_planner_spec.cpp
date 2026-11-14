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

// The live pitch shifter's hop planner (dsp-next/fixed/pitch_shift/hop_planner.h) against the old hop end it replaced
// (reference_hop_plan.h): the new head's cursor, phase and kind, the hop's length, threshold and next crossfade, and
// the noise drawn, over random old heads, shifts, crossfades and moments, mono and stereo, on several histories. The
// one difference is Q14's: a repitching head is held far enough behind the latest frame for the rest of the block.

#include "pitch_shift_histories.h"
#include "reference_hop_plan.h"

#include "dsp-next/fixed/pitch_shift/hop_planner.h"

#include <cppspec.hpp>
#include <cstdint>
#include <random>
#include <vector>

namespace {
namespace ps = deluge::dsp::fixed::pitch_shift;
namespace ph = pitch_shift_histories;
namespace pw = pitch_shift_workloads;
using deluge::synth::reference_hop::reference_plan;

// The old hop end's cursor, held behind the latest frame by the rest of the block's advance (Q14)
uint32_t held_behind(uint32_t cursor, uint32_t phase, int32_t shift, uint64_t now, uint64_t latest, std::size_t& held) {
	const uint64_t remaining = latest - now;
	const auto advance = static_cast<uint32_t>((phase + (remaining * static_cast<uint32_t>(shift))) >> 24);
	const ps::RingPosition last{static_cast<uint32_t>(latest)};
	const uint32_t behind = last.since(ps::RingPosition{cursor});
	if (behind > ps::kHistoryFrames / 2 || behind < advance) {
		++held;
		return (last - static_cast<int32_t>(advance)).index();
	}
	return cursor;
}

template <typename Frame>
bool plans_as_the_old_hop_end(uint32_t seed, std::size_t& aligned, std::size_t& held) {
	std::mt19937 rng(seed);
	const std::vector<int32_t> shifts{pw::kDown24, pw::kDown12, pw::kDown5, pw::kDown1, pw::kUnity, pw::kUpHalf,
	                                  pw::kUp1,    pw::kUp5,    pw::kUp7,   pw::kUp12,  pw::kUp24};
	for (const ph::HistoryCase& hc : ph::history_cases()) {
		const auto history = ph::history_of<Frame>(hc.signal, hc.frames);
		const uint64_t latest = history->frames_written();
		for (int k = 0; k < 400; ++k) {
			ps::PlayHeadState older{};
			const auto behind = std::uniform_int_distribution<uint32_t>(0, std::min<uint32_t>(hc.frames, 8191))(rng);
			older.cursor = ps::RingPosition{static_cast<uint32_t>(latest) - behind};
			older.phase = std::uniform_int_distribution<uint32_t>(0, (1U << 24) - 1)(rng);
			older.kind = (rng() & 3) != 0 ? ps::HeadKind::REPITCHING : ps::HeadKind::DIRECT;
			const int32_t shift = shifts[rng() % shifts.size()];
			const auto crossfade = std::uniform_int_distribution<uint32_t>(0, 400)(rng);
			const auto into_hop = std::uniform_int_distribution<int32_t>(0, 4000)(rng);
			const uint64_t now =
			    latest - std::uniform_int_distribution<uint64_t>(0, std::min<uint64_t>(latest, 128))(rng);
			deluge::dsp::fixed::Cong noise_a(static_cast<uint32_t>(rng()));
			deluge::dsp::fixed::Cong noise_b = noise_a;
			const ps::HopPlan got =
			    ps::HopPlanner::plan(shift, *history, older, crossfade, into_hop, now, latest, noise_a);
			const auto want = reference_plan(shift, *history, older, crossfade, into_hop, now, latest, noise_b);
			const uint32_t want_cursor = want.kind == ps::HeadKind::REPITCHING
			                                 ? held_behind(want.cursor, want.phase, shift, now, latest, held)
			                                 : want.cursor;
			if (got.cursor.index() != want_cursor || got.kind != want.kind || got.hop_length != want.hop_length
			    || got.tuning.percussive_threshold != want.percussive_threshold
			    || got.tuning.crossfade_length != want.next_crossfade_length || noise_a.state() != noise_b.state()) {
				return false;
			}
			if (got.kind == ps::HeadKind::REPITCHING && got.phase != want.phase) {
				return false;
			}
			if (got.phase != older.phase || got.cursor.index() != want_cursor) {
				++aligned; // The alignment moved the head or its phase: counted to show the aligner was reached
			}
		}
	}
	return true;
}
} // namespace

// clang-format off
describe hop_planner("deluge::dsp::fixed::pitch_shift::HopPlanner", ${
	it("plans each hop as the old hop end did, held behind the latest frame, mono", _ {
		std::size_t aligned = 0;
		std::size_t held = 0;
		expect(plans_as_the_old_hop_end<deluge::dsp::Q<31>>(0x11u, aligned, held)).to_be_true();
		expect(aligned).to_be_greater_than(std::size_t{100});
		expect(held).to_be_greater_than(std::size_t{0});
	});

	it("plans each hop as the old hop end did, held behind the latest frame, stereo", _ {
		std::size_t aligned = 0;
		std::size_t held = 0;
		expect(plans_as_the_old_hop_end<deluge::dsp::StereoSample>(0x22u, aligned, held)).to_be_true();
		expect(aligned).to_be_greater_than(std::size_t{100});
		expect(held).to_be_greater_than(std::size_t{0});
	});
});
// clang-format on

CPPSPEC_MAIN(hop_planner);
