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

#include "scenarios/support/scenario_report.h"

#include <cstdarg>
#include <ostream>

namespace deluge::scenarios {

void ScenarioReport::fail(const char* fmt, ...) {
	std::va_list args;
	va_start(args, fmt);
	std::va_list measure;
	va_copy(measure, args);
	const int size = std::vsnprintf(nullptr, 0, fmt, measure);
	va_end(measure);
	std::string what;
	if (size < 0) {
		what = std::string{"(unformattable) "} + fmt;
	}
	else if (size > 0) {
		what.resize(static_cast<size_t>(size));
		// Writes the terminator over what[size()], which holds '\0' already.
		std::vsnprintf(what.data(), what.size() + 1, fmt, args);
	}
	va_end(args);
	failures_++;
	// One write per line: Rust's println! shares the process's stdout, and a line must not be split around it.
	const std::string line = prefix_ + "-FAIL " + what + "\n";
	std::fputs(line.c_str(), out_);
	std::fflush(out_);
}

void ScenarioReport::printResult() const {
	std::fprintf(out_, "%s-RESULT failures=%d\n", prefix_.c_str(), static_cast<int>(failures_));
	std::fflush(out_);
}

std::ostream& operator<<(std::ostream& out, WaitState state) {
	switch (state) {
	case WaitState::Pending:
		return out << "Pending";
	case WaitState::Met:
		return out << "Met";
	case WaitState::Expired:
		return out << "Expired";
	}
	return out << "WaitState(" << static_cast<int>(state) << ")";
}

WaitState Wait::poll(bool met) {
	if (state_ != WaitState::Pending) {
		return state_;
	}
	if (met) {
		state_ = WaitState::Met;
	}
	else if (++ticks_ >= budget_) {
		state_ = WaitState::Expired;
		report_->fail("%s: not within %d ticks", what_, static_cast<int>(budget_));
	}
	return state_;
}

} // namespace deluge::scenarios
