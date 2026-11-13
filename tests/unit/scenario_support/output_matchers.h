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

#include "cppspec.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

/// @file
/// @brief CppSpec matchers over captured text output, one line per `\n`.
namespace deluge::test {

/// @brief @p text split into lines, without their newlines; a trailing line with no newline counts.
inline std::vector<std::string> linesOf(std::string_view text) {
	std::vector<std::string> lines;
	while (!text.empty()) {
		const size_t end = text.find('\n');
		lines.emplace_back(text.substr(0, end));
		text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
	}
	return lines;
}

/// @brief Matches output holding @p line as one whole line.
///
/// `auto out = expect(text); out.to(HaveLine(out, "SR-FAIL x"));`
class HaveLine : public CppSpec::Matchers::MatcherBase<std::string, std::string> {
public:
	HaveLine(CppSpec::Expectation<std::string>& expectation, std::string line)
	    : MatcherBase(expectation, std::move(line)) {}

	std::string verb() override { return "have the line"; }

	std::string failure_message() override {
		return std::format("expected the line \"{}\" in:\n{}", expected(), actual());
	}

	bool match() override { return std::ranges::contains(linesOf(actual()), expected()); }
};

/// @brief Matches output with exactly @p count lines starting with @p prefix.
class HaveLinesStartingWith : public CppSpec::Matchers::MatcherBase<std::string, std::string> {
public:
	HaveLinesStartingWith(CppSpec::Expectation<std::string>& expectation, std::string prefix, size_t count)
	    : MatcherBase(expectation, std::move(prefix)), count_{count} {}

	std::string verb() override { return std::format("have {} line(s) starting with", count_); }

	std::string failure_message() override {
		return std::format("expected {} line(s) starting with \"{}\", found {} in:\n{}", count_, expected(), found(),
		                   actual());
	}

	bool match() override { return found() == count_; }

private:
	size_t found() {
		return static_cast<size_t>(std::ranges::count_if(
		    linesOf(actual()), [&](const std::string& line) { return line.starts_with(expected()); }));
	}
	size_t count_;
};

} // namespace deluge::test
