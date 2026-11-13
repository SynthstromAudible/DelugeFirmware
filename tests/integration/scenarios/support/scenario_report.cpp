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
	std::string what(size > 0 ? static_cast<size_t>(size) : 0, '\0');
	if (size > 0) {
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
