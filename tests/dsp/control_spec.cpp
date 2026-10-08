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
#include "dsp-next/core/concepts.h"
#include "dsp-next/core/control.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/types.h"
#include "util/fixedpoint.h"

#include <concepts>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

using namespace deluge::dsp;

namespace {

/// A level that can be moved but not copied, so an event taking it by value receives what `on` forwards.
struct MoveOnlyLevel {
	q31 value; ///< Q1.31.
	explicit MoveOnlyLevel(q31 v) : value{v} {}
	MoveOnlyLevel(MoveOnlyLevel&&) = default;
	MoveOnlyLevel(const MoveOnlyLevel&) = delete;
	MoveOnlyLevel& operator=(MoveOnlyLevel&&) = default;
	MoveOnlyLevel& operator=(const MoveOnlyLevel&) = delete;
	~MoveOnlyLevel() = default;
};

/// A control kernel without modes: a level falling by its rate per sample from full scale, saturating at 0, that
/// counts its own calls.
struct Countdown {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 rate; ///< Fall per sample, Q1.31.
	};
	struct Coefficients {
		q31 rate; ///< Fall per sample, Q1.31.
	};
	struct State {
		q31 level = ONE_Q31; ///< Q1.31.
		uint32_t calls;      ///< How many times `advance` has run.
	};
	static Coefficients coefficients(const Settings& s) { return {.rate = s.rate}; }
	static q31 advance(const Coefficients& c, State& s, std::size_t n) {
		const int64_t next = int64_t{s.level} - (int64_t{c.rate} * static_cast<int64_t>(n));
		s.level = next < 0 ? 0 : static_cast<q31>(next);
		++s.calls;
		return s.level;
	}
	/// Sets the level to @p to.
	static void kick(State& s, q31 to) { s.level = to; }
	/// Lowers the level by the configured rate times @p by.
	static void kick_scaled(const Coefficients& c, State& s, q31 by) { s.level -= c.rate * by; }
	/// Sets the level to @p to and returns the level it replaced.
	static q31 swap_level(State& s, q31 to) {
		const q31 old = s.level;
		s.level = to;
		return old;
	}
	/// Returns the state itself, so that the caller sees the wrapper's own state.
	static State& self(State& s) { return s; }
	/// Sets the level from a move-only argument, which `on` must forward rather than copy.
	static void kick_owned(State& s, MoveOnlyLevel to) { s.level = to.value; }
	/// Takes the state by value: it would change a copy, so `on` rejects it.
	static void by_copy(State s, q31 to) { s.level = to; }
	/// Takes the state by const reference: it cannot change it, so `on` rejects it.
	static q31 by_const(const Coefficients& /*c*/, const State& s) { return s.level; }
};

/// A moded control kernel: its level falls (DOWN) or rises (UP) by its rate per sample.
struct SignedCountdown {
	using value_type = q31;
	using step_type = q31;
	enum class Dir : uint8_t { DOWN, UP };
	using Modes = ModeList<Dir::DOWN, Dir::UP>;
	struct Settings {
		q31 rate; ///< Change per sample.
		Dir dir;  ///< Which way it changes.
	};
	struct Coefficients {
		q31 rate;             ///< Change per sample.
		Dir mode = Dir::DOWN; ///< Which way it changes.
	};
	struct State {
		q31 level = 1000; ///< The current level.
	};
	static Coefficients coefficients(const Settings& s) { return {.rate = s.rate, .mode = s.dir}; }
	template <Dir M>
	static q31 advance(const Coefficients& c, State& s, std::size_t n) {
		const auto change = static_cast<q31>(c.rate * static_cast<q31>(n));
		if constexpr (M == Dir::DOWN) {
			s.level -= change;
		}
		else {
			s.level += change;
		}
		return s.level;
	}
};

/// A control kernel whose reset is a quirk: it restarts at 7, not at State{}.
struct RestartsAtSeven {
	using value_type = q31;
	using step_type = q31;
	struct Settings {
		q31 rate; ///< Rise per sample.
	};
	struct Coefficients {
		q31 rate; ///< Rise per sample.
	};
	struct State {
		q31 level; ///< The current level.
	};
	static Coefficients coefficients(const Settings& s) { return {.rate = s.rate}; }
	static void reset(State& s) { s.level = 7; }
	static q31 advance(const Coefficients& c, State& s, std::size_t n) {
		s.level += c.rate * static_cast<q31>(n);
		return s.level;
	}
};

/// A fall per sample of 2^-11 of full scale, Q1.31.
constexpr q31 kRate = 1 << 20;

static_assert(control_kernel<Countdown>);
static_assert(control_kernel<SignedCountdown> && moded_kernel<SignedCountdown>);
static_assert(control_kernel<RestartsAtSeven> && detail::declares_reset<RestartsAtSeven>);
static_assert(control_generator<Control<Countdown>>);
static_assert(control_generator<Control<SignedCountdown>>);
static_assert(!std::is_polymorphic_v<Control<Countdown>>);
static_assert(detail::event_takes_state<Countdown, &Countdown::kick, q31>);
static_assert(detail::event_takes_coefficients<Countdown, &Countdown::kick_scaled, q31>);
static_assert(!detail::event_takes_coefficients<Countdown, &Countdown::kick, q31>);
static_assert(!detail::event_takes_state<Countdown, &Countdown::by_copy, q31>);
static_assert(!detail::event_takes_coefficients<Countdown, &Countdown::by_copy, q31>);
static_assert(!detail::event_takes_coefficients<Countdown, &Countdown::by_const>);
static_assert(!detail::event_takes_state<Countdown, &Countdown::by_const>);

} // namespace

// clang-format off
describe control("Control", ${
	it("advances a block at a time, one kernel call per advance, an empty block included", _ {
		Control<Countdown> countdown;
		countdown.configure({.rate = kRate});
		expect(countdown.advance(4)).to_equal(ONE_Q31 - (4 * kRate));
		expect(countdown.advance(0)).to_equal(ONE_Q31 - (4 * kRate));
		expect(countdown.advance(7)).to_equal(ONE_Q31 - (11 * kRate));
		expect(countdown.state().level).to_equal(2135949311);
		expect(countdown.state().calls).to_equal(3U);
	});

	it("saturates as its kernel does", _ {
		Control<Countdown> countdown{Countdown::Settings{.rate = ONE_Q31}};
		expect(countdown.advance(2)).to_equal(0);
		expect(countdown.coefficients().rate).to_equal(ONE_Q31);
	});

	it("uses a configure between blocks from the next block on, its state carried over", _ {
		Control<Countdown> countdown{Countdown::Settings{.rate = kRate}};
		std::ignore = countdown.advance(4);
		countdown.configure({.rate = 3 * kRate});
		expect(countdown.advance(4)).to_equal(ONE_Q31 - (4 * kRate) - (4 * 3 * kRate));
		expect(countdown.state().level).to_equal(2130706431);
		expect(countdown.state().calls).to_equal(2U);
	});

	it("runs each block in its own mode, the state carried across a switch", _ {
		Control<SignedCountdown> signed_countdown;
		expect(signed_countdown.advance(3)).to_equal(1000);
		signed_countdown.configure({.rate = 10, .dir = SignedCountdown::Dir::DOWN});
		expect(signed_countdown.advance(3)).to_equal(970);
		signed_countdown.configure({.rate = 10, .dir = SignedCountdown::Dir::UP});
		expect(signed_countdown.advance(3)).to_equal(1000);
		expect(signed_countdown.advance(2)).to_equal(1020);
	});

	it("runs an event on the state, with the coefficients when the event takes them", _ {
		Control<Countdown> countdown{Countdown::Settings{.rate = kRate}};
		countdown.on<&Countdown::kick>(1000000000);
		expect(countdown.state().level).to_equal(1000000000);
		countdown.on<&Countdown::kick_scaled>(2);
		expect(countdown.state().level).to_equal(1000000000 - (2 * kRate));
		expect(countdown.advance(1)).to_equal(1000000000 - (3 * kRate));
	});

	it("returns an event's result", _ {
		Control<Countdown> countdown;
		static_assert(std::same_as<decltype(countdown.on<&Countdown::swap_level>(q31{})), q31>);
		static_assert(std::same_as<decltype(countdown.on<&Countdown::kick>(q31{})), void>);
		expect(countdown.on<&Countdown::swap_level>(5)).to_equal(ONE_Q31);
		expect(countdown.on<&Countdown::swap_level>(9)).to_equal(5);
		expect(countdown.state().level).to_equal(9);
	});

	it("returns a reference an event returns, to the wrapper's own state", _ {
		Control<Countdown> countdown;
		static_assert(std::same_as<decltype(countdown.on<&Countdown::self>()), Countdown::State&>);
		Countdown::State& state = countdown.on<&Countdown::self>();
		expect(&state).to_equal(&countdown.state());
	});

	it("forwards a move-only argument to the event", _ {
		Control<Countdown> countdown;
		countdown.on<&Countdown::kick_owned>(MoveOnlyLevel{42});
		expect(countdown.state().level).to_equal(42);
		MoveOnlyLevel held{43};
		countdown.on<&Countdown::kick_owned>(std::move(held));
		expect(countdown.state().level).to_equal(43);
	});

	it("resets to State{}, keeping its coefficients", _ {
		Control<Countdown> countdown{Countdown::Settings{.rate = kRate}};
		std::ignore = countdown.advance(4);
		countdown.reset();
		expect(countdown.state().level).to_equal(ONE_Q31);
		expect(countdown.state().calls).to_equal(0U);
		expect(countdown.coefficients().rate).to_equal(kRate);
		expect(countdown.advance(1)).to_equal(ONE_Q31 - kRate);
	});

	it("resets through the kernel's reset quirk when it declares one", _ {
		Control<RestartsAtSeven> restarts{RestartsAtSeven::Settings{.rate = 2}};
		expect(restarts.advance(3)).to_equal(6);
		restarts.reset();
		expect(restarts.state().level).to_equal(7);
		expect(restarts.coefficients().rate).to_equal(2);
		expect(restarts.advance(1)).to_equal(9);
	});
});

CPPSPEC_MAIN(control);
