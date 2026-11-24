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

// The time stretcher's hop clock, hop budget and lent cache-tail buffers (synth/sources/sample/hop_clock.h and
// stretch_context.h) against the legacy's records: the first hop (S1), a fudge's start (S2) and the hop check (S3).

#include "synth/sources/sample/hop_clock.h"
#include "synth/sources/sample/stretch_context.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <optional>
#include <set>
#include <type_traits>

#include "time_stretcher_records.h"

#include "support/spec_main.h"

namespace {
namespace r = time_stretcher_records;
using deluge::dsp::fixed::sample_read::Increment;
using deluge::synth::fudge_into;
using deluge::synth::HopBudget;
using deluge::synth::HopClock;
using deluge::synth::TailBuffers;
using deluge::synth::TailLoan;

static_assert(std::is_trivially_copyable_v<HopClock> && std::is_trivially_copyable_v<HopBudget>);
static_assert(sizeof(TailBuffers) <= 16 * 1024 + 8);

Increment increment(uint32_t raw) {
	return Increment::from_raw(raw);
}

HopBudget budget_at(uint8_t hops) {
	HopBudget budget;
	budget.hops = hops;
	return budget;
}

// The S3 rows the clock and budget disagree with: the turn, the bookkeeping and the segment.
std::size_t hop_check_mismatches() {
	std::size_t bad = 0;
	for (const auto& row : r::kHopCheckGoldens) {
		HopBudget budget = budget_at(row.hops);
		HopClock clock = HopClock::at(row.to_hop, row.passed_over);
		const HopClock::Segment s = clock.segment(budget, row.rest);
		const bool due = s.turn != HopClock::Turn::NOT_DUE;
		bad += (due != row.due) ? 1 : 0;
		int32_t frames = s.frames;
		if (s.turn == HopClock::Turn::HOP) {
			// The hop replaces the clock with its own; the segment then runs to it
			bad += row.allowed ? 0 : 1;
			clock = HopClock::after(row.to_hop_after + row.segment);
			frames = clock.frames(row.rest);
		}
		else if (due) {
			bad += row.allowed == (s.turn == HopClock::Turn::HOP) ? 0 : 1;
		}
		bad += frames == row.segment ? 0 : 1;
		clock.advance(frames);
		bad += budget.hops == row.hops_after ? 0 : 1;
		bad += clock.passed_over() == row.passed_over_after ? 0 : 1;
		bad += clock.to_hop() == row.to_hop_after ? 0 : 1;
	}
	return bad;
}
} // namespace

// clang-format off
describe hop_clock_spec_body("deluge::synth::HopClock", ${
	it("draws the first hop as the legacy did (S1)", _ {
		std::size_t bad = 0;
		for (const auto& row : r::kFirstHopGoldens) {
			bad += HopClock::first_hop(row.draw).to_hop() == row.first_hop ? 0 : 1;
		}
		expect(bad).to_equal(std::size_t{0});
		expect(HopClock::first_hop(0x80000000u).to_hop()).to_equal(168);
		expect(HopClock::first_hop(0x7F000000u).to_hop()).to_equal(231);
		expect(HopClock::first_hop(0x00FFFFFFu).to_hop()).to_equal(200);
	});

	it("starts a fudge's new head as the legacy did (S2)", _ {
		std::size_t bad = 0;
		for (const auto& row : r::kFudgeGoldens) {
			bad += fudge_into(row.frames_to_loop, increment(row.increment)) == row.into ? 0 : 1;
		}
		expect(bad).to_equal(std::size_t{0});
		expect(fudge_into(100, increment(1u << 24))).to_equal(100);
		expect(fudge_into(100, increment(1u << 25))).to_equal(200);
		expect(HopClock::never().to_hop()).to_equal(INT32_MAX);
		expect(HopClock::retry().to_hop()).to_equal(500);
	});

	it("lets a hop through the budget as the legacy did (S3)", _ {
		expect(hop_check_mismatches()).to_equal(std::size_t{0});
	});

	it("passes a waiting hop over until twice the routine's hops", _ {
		HopBudget one = budget_at(1);
		HopClock clock = HopClock::at(0, 1);
		expect(static_cast<int32_t>(clock.segment(one, 128).turn)).to_equal(static_cast<int32_t>(HopClock::Turn::WAIT));
		expect(static_cast<int32_t>(clock.passed_over())).to_equal(2);
		expect(static_cast<int32_t>(clock.segment(one, 128).turn)).to_equal(static_cast<int32_t>(HopClock::Turn::HOP));
		expect(static_cast<int32_t>(clock.passed_over())).to_equal(0);
		expect(one.culling_bypassed ? 1 : 0).to_equal(1);
		expect(static_cast<int32_t>(one.hops)).to_equal(2);

		HopBudget three = budget_at(3);
		HopClock waiting = HopClock::at(-5, 200);
		const HopClock::Segment s = waiting.segment(three, 128);
		expect(static_cast<int32_t>(s.turn)).to_equal(static_cast<int32_t>(HopClock::Turn::WAIT));
		expect(s.frames).to_equal(128); // not allowed: the rest of the block, so the clock runs on past 0
		expect(three.culling_bypassed ? 1 : 0).to_equal(0);

		HopBudget fresh;
		HopClock early = HopClock::at(40, 0);
		const HopClock::Segment n = early.segment(fresh, 128);
		expect(static_cast<int32_t>(n.turn)).to_equal(static_cast<int32_t>(HopClock::Turn::NOT_DUE));
		expect(n.frames).to_equal(40);
		fresh.spend();
		fresh.new_routine();
		expect(static_cast<int32_t>(fresh.hops)).to_equal(0);
		expect(fresh.culling_bypassed ? 1 : 0).to_equal(0);
	});

	it("lends eight tails and no ninth", _ {
		static TailBuffers tails;
		std::array<std::optional<TailLoan>, 8> loans;
		std::set<uint8_t> slots;
		for (auto& loan : loans) {
			loan = tails.lend();
			expect(loan.has_value() ? 1 : 0).to_equal(1);
			expect(static_cast<int32_t>(loan->words.size())).to_equal(512);
			slots.insert(loan->slot);
		}
		expect(static_cast<int32_t>(slots.size())).to_equal(8);
		expect(tails.free_count()).to_equal(0);
		expect(tails.lend().has_value() ? 1 : 0).to_equal(0);
		for (const auto& loan : loans) {
			if (loan->slot == 3) {
				tails.give_back(*loan);
			}
		}
		expect(tails.free_count()).to_equal(1);
		const std::optional<TailLoan> again = tails.lend();
		expect(static_cast<int32_t>(again->slot)).to_equal(3);
		expect(tails.free_count()).to_equal(0);
	});
});
// clang-format on

DELUGE_SPEC(hop_clock_spec, hop_clock_spec_body)
