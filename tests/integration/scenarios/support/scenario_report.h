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

#pragma once

#include <cstdint>
#include <cstdio>
#include <iosfwd>
#include <string>
#include <string_view>

/// @file
/// @brief A scenario's failures and result line, and the bounded waits its steps poll.
///
/// Every failure a scenario counts is one line, `<PREFIX>-FAIL <what>`, where PREFIX is the scenario's marker prefix
/// (the one its result line carries). A failure that belongs to a stage names it first in `<what>`, without the
/// prefix: `SP-FAIL ACQUIRE path=…`. The result line is `<PREFIX>-RESULT failures=<n>`.
///
/// A scenario is stepped one tick at a time, so a wait is an object a step polls, not a loop: a stage polls its `Wait`
/// with whether its condition holds and returns while the answer is `Pending`. A wait whose budget runs out records a
/// failure naming it, so a stage that then reads state reads it knowing the wait failed, never as if it had ended.
namespace deluge::scenarios {

class ScenarioReport {
public:
	/// @param prefix The scenario's marker prefix, without the dash ("SR").
	/// @param out Where its lines go; stdout in a scenario, a temporary file in its spec.
	explicit ScenarioReport(std::string_view prefix, std::FILE* out = stdout) : prefix_{prefix}, out_{out} {}

	/// @brief Prints `<PREFIX>-FAIL <what>`, @p fmt formatted as printf formats it, and counts one failure.
	[[gnu::format(__printf__, 2, 3)]] void fail(const char* fmt, ...);

	/// @brief Counts @p n failures whose lines are printed elsewhere: an evidence line that carries its own verdict
	/// (`ATH-IDLE ok=0`), or a shared checker that prints its own (`expectSongCoherent`).
	void count(int32_t n = 1) { failures_ += n; }

	[[nodiscard]] int32_t failures() const { return failures_; }
	[[nodiscard]] bool clean() const { return failures_ == 0; }
	[[nodiscard]] std::string_view prefix() const { return prefix_; }

	/// @brief Zeroes the count, for a scenario's `begin`.
	void reset() { failures_ = 0; }

	/// @brief Prints `<PREFIX>-RESULT failures=<n>`.
	void printResult() const;

private:
	std::string prefix_;
	std::FILE* out_;
	int32_t failures_ = 0;
};

enum class WaitState : uint8_t {
	/// The condition has not held yet and the budget has not run out: poll again next step.
	Pending,
	/// The condition held on the last poll.
	Met,
	/// The budget ran out first; the failure is recorded.
	Expired,
};

std::ostream& operator<<(std::ostream& out, WaitState state);

/// A wait for a condition, bounded by a number of steps. One `poll` per step; polls after it has ended return the
/// state it ended in and count nothing.
class Wait {
public:
	/// @param what What is awaited, as the failure names it ("the staged open issuing its read"): a literal, or
	/// storage that outlives the Wait (not a temporary's c_str()).
	/// @param budgetTicks Unmet polls allowed; the poll that makes them this many expires the wait.
	Wait(ScenarioReport& report, const char* what, int32_t budgetTicks)
	    : report_{&report}, what_{what}, budget_{budgetTicks} {}

	/// @brief One step's poll, with whether the condition holds now.
	WaitState poll(bool met);

	/// @brief `poll(met)` is no longer Pending: met or expired.
	[[nodiscard]] bool done(bool met) { return poll(met) != WaitState::Pending; }

	[[nodiscard]] WaitState state() const { return state_; }
	[[nodiscard]] bool expired() const { return state_ == WaitState::Expired; }
	/// @brief Unmet polls so far: the ticks the wait took when it is met.
	[[nodiscard]] int32_t ticks() const { return ticks_; }
	[[nodiscard]] int32_t budget() const { return budget_; }

	/// @brief Pending again, with no ticks spent.
	void restart() {
		state_ = WaitState::Pending;
		ticks_ = 0;
	}

private:
	ScenarioReport* report_;
	const char* what_;
	int32_t budget_;
	int32_t ticks_ = 0;
	WaitState state_ = WaitState::Pending;
};

} // namespace deluge::scenarios
