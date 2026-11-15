// clang-format off
#include "controls/takeover.h"
#include <cppspec.hpp>
#include <cstdint>
#include <optional>
#include <vector>

using namespace deluge::controls;

namespace {
/// A controller's values through one knob that starts at @p knob_pos, each answer the knob's position for the next.
std::vector<int32_t> walk(int32_t knob_pos, TakeoverInputs inputs, TakeoverState state, std::vector<uint8_t> values) {
	std::vector<int32_t> seen;
	for (const uint8_t value : values) {
		knob_pos = knob_pos_after(knob_pos, value, inputs, state);
		seen.push_back(knob_pos);
	}
	return seen;
}
using Positions = std::vector<int32_t>;
constexpr TakeoverInputs kJump{.mode = TakeoverMode::JUMP};
constexpr TakeoverInputs kPickup{.mode = TakeoverMode::PICKUP};
constexpr TakeoverInputs kScale{.mode = TakeoverMode::SCALE};
constexpr TakeoverInputs kRelative{.mode = TakeoverMode::RELATIVE};
} // namespace

describe takeover("MIDI takeover as a pure function", ${
	it("jumps to the controller's position in JUMP mode, and 127 reads as +64", _{
		expect(walk(0, kJump, {}, {0, 127, 64, 100})).to_equal(Positions{-64, 64, 0, 36});
	});
	it("follows a relative reading by its offset, from the cable, the knob or the mode, in every mode", _{
		for (const TakeoverMode mode : {TakeoverMode::JUMP, TakeoverMode::PICKUP, TakeoverMode::SCALE}) {
			expect(walk(0, {.mode = mode, .cable_relative = true}, {}, {1, 127, 10, 100})).to_equal(Positions{1, 0, 10, -18});
			expect(walk(0, {.mode = mode, .knob_relative = true}, {}, {1, 127, 10, 100})).to_equal(Positions{1, 0, 10, -18});
		}
		expect(walk(0, kRelative, {}, {1, 127, 10, 100})).to_equal(Positions{1, 0, 10, -18});
	});
	it("holds a relative reading to 64 above, and below to -64 or the knob's own lower position", _{
		expect(walk(60, kRelative, {}, {10})).to_equal(Positions{64});
		expect(walk(-60, kRelative, {}, {100})).to_equal(Positions{-64});
		// Past the range already: a turn down is stopped where it is, a turn up is let through.
		expect(walk(-100, kRelative, {}, {100, 5})).to_equal(Positions{-100, -95});
	});
	it("jumps while recording or step editing, in PICKUP and SCALE modes", _{
		for (const TakeoverMode mode : {TakeoverMode::PICKUP, TakeoverMode::SCALE}) {
			expect(walk(20, {.mode = mode, .recording = true}, {}, {0})).to_equal(Positions{-64});
			expect(walk(20, {.mode = mode, .step_editing = true}, {}, {0})).to_equal(Positions{-64});
			expect(walk(20, {.mode = mode}, {}, {0})).to_equal(Positions{20});
		}
	});
	it("remembers the position it took, and the last controller position after a pickup or scale reading", _{
		TakeoverState state;
		expect(knob_pos_after(20, 54, kPickup, state)).to_equal(20);
		expect(state.previous).to_equal(std::optional<int32_t>{-10});
		expect(knob_pos_after(0, 3, kRelative, state)).to_equal(3);
		expect(state.previous).to_equal(std::optional<int32_t>{3});
		expect(knob_pos_after(0, 100, kJump, state)).to_equal(36);
		expect(state.previous).to_equal(std::optional<int32_t>{36});
	});
	it("seeds the remembered position with the first value, and so picks up at once", _{
		TakeoverState state;
		expect(knob_pos_after(20, 0, kPickup, state)).to_equal(20);
		expect(state.previous).to_equal(std::optional<int32_t>{-64});
		TakeoverState scaled;
		expect(knob_pos_after(20, 0, kScale, scaled)).to_equal(20);
		expect(scaled.previous).to_equal(std::optional<int32_t>{-64});
	});
	it("picks up in PICKUP mode only when the controller crosses the knob, from either side", _{
		// From below: the controller starts under the knob and has to reach it.
		expect(walk(20, kPickup, {}, {54, 74, 89, 94})).to_equal(Positions{20, 20, 25, 30});
		// From above: the controller starts over the knob, and the pickup takes it on the way down.
		expect(walk(-20, kPickup, {.previous = 30}, {90, 60, 40, 30})).to_equal(Positions{-20, -20, -24, -34});
	});
	it("holds a first PICKUP value that starts on the far side of the knob, until the controller crosses it", _{
		// Seeded with its own position (-54, below the knob), not with 0 (above it): no crossing yet.
		expect(walk(-20, kPickup, {}, {10, 30})).to_equal(Positions{-20, -20});
		expect(walk(-20, kPickup, {}, {10, 50})).to_equal(Positions{-20, -14});
	});
	it("meets the knob exactly when the controller lands on it", _{
		expect(walk(20, kPickup, {.previous = 0}, {84})).to_equal(Positions{20});
		expect(walk(20, kScale, {.previous = 0}, {84})).to_equal(Positions{20});
	});
	it("scales the runway in SCALE mode, rounding the float", _{
		// Left from 0 to -30: 20 + 84 * (-30 / 34) = -54.1; then right to -10: -54 + 118 * (20 / 74) = -22.1.
		expect(walk(20, kScale, {.previous = 0}, {34, 54})).to_equal(Positions{-54, -22});
	});
	it("rounds a scaled half away from zero", _{
		// Right from 55 to 56: 60 + 4 * (1 / 8) = 60.5.
		expect(walk(60, kScale, {.previous = 55}, {120})).to_equal(Positions{61});
		// Left from -55 to -56: -60 + 4 * (-1 / 8) = -60.5.
		expect(walk(-60, kScale, {.previous = -55}, {8})).to_equal(Positions{-61});
	});
	it("keeps a small share of the runway from moving the knob a whole step", _{
		expect(walk(60, kScale, {.previous = -64}, {1})).to_equal(Positions{60});
	});
	it("clamps a scaled knob to the ends of its range", _{
		// 10 + 54 * (42 / 2) is far past 64.
		expect(walk(10, kScale, {.previous = 20}, {126})).to_equal(Positions{64});
		// 10 + 74 * (-32 / 2) is far past -64.
		expect(walk(10, kScale, {.previous = -30}, {2})).to_equal(Positions{-64});
	});
	it("leaves a knob at an end of its range there, whichever way the controller turns", _{
		expect(walk(64, kScale, {.previous = -30}, {100})).to_equal(Positions{64});
		expect(walk(-64, kScale, {.previous = 30}, {10})).to_equal(Positions{-64});
	});
	it("never moves a knob already out of range back toward it past where it was, before the clamp", _{
		// 70 + (64 - 70) * (66 / 28) = 55.9 would be below the knob turning right; it stays and clamps to 64.
		expect(walk(70, kScale, {.previous = -30}, {100})).to_equal(Positions{64});
		// -70 + (-70 + 64) * (-84 / 10) = -19.6 would be above the knob turning left; it stays and clamps to -64.
		expect(walk(-70, kScale, {.previous = 30}, {10})).to_equal(Positions{-64});
	});
	it("does not scale when the controller has no runway left", _{
		// The controller turns right to its top (127 reads +64) and has no runway up: the knob stays.
		expect(walk(10, kScale, {.previous = 30}, {127})).to_equal(Positions{10});
		// The controller turns left to its bottom and has no runway down: the knob stays.
		expect(walk(-10, kScale, {.previous = -30}, {0})).to_equal(Positions{-10});
	});
	it("leaves the knob where it is when the controller has not moved", _{
		expect(walk(20, kScale, {.previous = -30}, {34})).to_equal(Positions{20});
	});
});
// clang-format on

CPPSPEC_SPEC(takeover)
