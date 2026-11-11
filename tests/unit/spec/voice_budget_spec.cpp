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
// The voice budget's pure pieces: a Sound's measured voice cost, the block budget's admission, and the backstop's cull.

#include "processing/engines/voice_budget.h"

#include "cppspec.hpp"

using deluge::audio::CullPlan;
using deluge::audio::plan_cull;
using deluge::audio::VoiceBudget;
using deluge::audio::VoiceCost;

namespace {
constexpr uint32_t kHz = 400'000'000;
constexpr uint32_t kBlock = 1'160'997; // 400 MHz × 128 / 44100
} // namespace

// clang-format off
describe voice_cost("deluge::audio::VoiceCost", ${
	it("reads the prior until a Sound has rendered four blocks of one voice", _ {
		VoiceCost cost;
		cost.add_render(30'000, 3 * 128);
		expect(cost.measured()).to_be_false();
		expect(cost.per_block(50'000)).to_equal(50'000u);
	});

	it("scales its voices' cycles to one voice for one 128-frame block", _ {
		VoiceCost cost;
		cost.add_render(80'000, 4 * 128); // four voices, one 128-frame window
		expect(cost.measured()).to_be_true();
		expect(cost.per_block(50'000)).to_equal(20'000u);
	});

	it("reads the same whatever windows the voices were rendered in", _ {
		VoiceCost cost;
		cost.add_render(10'000, 2 * 64);
		cost.add_render(10'000, 2 * 64);
		cost.add_render(20'000, 2 * 128);
		expect(cost.per_block(0)).to_equal(10'000u); // 40,000 cycles over 512 voice-frames
	});

	it("moves halfway to each new measurement", _ {
		VoiceCost cost;
		cost.add_render(80'000, 4 * 128);
		cost.add_render(160'000, 4 * 128);
		expect(cost.per_block(0)).to_equal(30'000u);
	});

	it("ignores a render with no voice-frames", _ {
		VoiceCost cost;
		cost.add_render(1'000, 0);
		expect(cost.measured()).to_be_false();
	});
});

describe voice_budget("deluge::audio::VoiceBudget", ${
	it("admits everything when there is no cycle counter (the host)", _ {
		VoiceBudget budget;
		budget.configure(0, 85);
		expect(budget.enabled()).to_be_false();
		expect(budget.admit(0xFFFF'FFFFu)).to_be_true();
		expect(budget.held_samples()).to_equal(0);
	});

	it("sets its capacity to the percentage of a block period", _ {
		VoiceBudget budget;
		budget.configure(kHz, 85);
		expect(budget.capacity()).to_equal(kBlock * 85 / 100);
	});

	it("admits a voice that fits on top of the last block, and counts it for the next", _ {
		VoiceBudget budget;
		budget.configure(kHz, 85);
		budget.end_block(kBlock / 2);
		expect(budget.admit(kBlock / 4)).to_be_true();
		expect(budget.admit(kBlock / 4)).to_be_false(); // 50% + 25% + 25% > 85%
	});

	it("starts each block's admissions from the held load, not from what was admitted", _ {
		VoiceBudget budget;
		budget.configure(kHz, 85);
		budget.end_block(kBlock / 2);
		expect(budget.admit(kBlock / 4)).to_be_true();
		budget.end_block(kBlock / 2);
		expect(budget.admit(kBlock / 4)).to_be_true();
	});

	it("commits a voice started over budget, so later admissions see it", _ {
		VoiceBudget budget;
		budget.configure(kHz, 85);
		budget.end_block(kBlock / 2);
		budget.commit(kBlock / 4);
		expect(budget.admit(kBlock / 4)).to_be_false();
	});

	it("holds the peak and lets it fall by a sixteenth a block", _ {
		VoiceBudget budget;
		budget.configure(kHz, 85);
		budget.end_block(1'600'000);
		budget.end_block(0);
		expect(budget.held_samples()).to_equal(static_cast<int32_t>((1'600'000u - 100'000u) * 44100ull / kHz));
	});

	it("reads the held and the last block's load as samples of a 128-frame block", _ {
		VoiceBudget budget;
		budget.configure(kHz, 85);
		budget.end_block(kBlock);
		expect(budget.held_samples()).to_equal(127); // 1,160,997 × 44100 / 4e8 = 127.99, truncated
		expect(budget.last_samples()).to_equal(127);
	});
});

describe cull_plan("deluge::audio::plan_cull", ${
	it("culls nothing while the render fits its period", _ {
		const CullPlan plan = plan_cull(-10, -5, false, 20);
		expect(plan.kill + plan.terminate + plan.force_release).to_equal(0);
	});

	it("hard-culls a sixteenth of the overshoot: one released, the rest terminated and killed half and half", _ {
		const CullPlan plan = plan_cull(160, 0, false, 40); // 10 voices
		expect(plan.force_release).to_equal(1);
		expect(plan.kill).to_equal(4);
		expect(plan.terminate).to_equal(5);
	});

	it("leaves the minimum voices", _ {
		const CullPlan plan = plan_cull(160, 0, false, 10); // 3 above the minimum of 7
		expect(plan.kill + plan.terminate + plan.force_release).to_equal(3);
	});

	it("soft-culls one voice only while a small overshoot is growing", _ {
		expect(plan_cull(10, 5, false, 20).force_release).to_equal(1);
		expect(plan_cull(10, 12, false, 20).force_release).to_equal(0);
		expect(plan_cull(10, -1, false, 20).force_release).to_equal(0);
	});

	it("reads an overrun as a hard overshoot, however the render measured", _ {
		const CullPlan plan = plan_cull(-50, 0, true, 20);
		expect(plan.force_release).to_equal(1);
		expect(plan.terminate).to_equal(1);
	});

	it("releases one voice under the minimum only when far over", _ {
		expect(plan_cull(40, 0, false, 5).force_release).to_equal(1);
		expect(plan_cull(39, 0, false, 5).force_release).to_equal(0);
	});
});
// clang-format on

CPPSPEC_SPEC(voice_budget)
