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

#include "output_matchers.h"

#include "cppspec.hpp"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

using deluge::scenarios::ScenarioReport;
using deluge::scenarios::Wait;
using deluge::scenarios::WaitState;
using deluge::test::HaveLine;
using deluge::test::HaveLinesStartingWith;

namespace {

/// A temporary file; a spec that cannot make one cannot capture anything, so it aborts.
std::FILE* openTemporary() {
	std::FILE* file = std::tmpfile();
	if (file == nullptr) {
		std::perror("scenario_report_spec: tmpfile");
		std::abort();
	}
	return file;
}

/// A report writing into a temporary file, and what it wrote.
struct Captured {
	std::FILE* file = openTemporary();
	ScenarioReport report{"SR", file};

	std::string text() {
		std::fflush(file);
		std::rewind(file);
		std::string out;
		for (int c = std::fgetc(file); c != EOF; c = std::fgetc(file)) {
			out.push_back(static_cast<char>(c));
		}
		return out;
	}
	~Captured() { std::fclose(file); }
};

std::string named(WaitState state) {
	std::ostringstream out;
	out << state;
	return out.str();
}

} // namespace

// clang-format off
describe scenario_report("deluge::scenarios", $ {
	context("ScenarioReport", _ {
	it("prints a failure as <prefix>-FAIL <what> and counts it", _ {
		Captured c;
		c.report.fail("the fixture %s could not be written (%d)", "DOC.XML", 7);
		expect(c.text()).to_equal(std::string{"SR-FAIL the fixture DOC.XML could not be written (7)\n"});
		expect(c.report.failures()).to_equal(int32_t{1});
		expect(c.report.clean()).to_be_false();
	});

	it("prints a message longer than any fixed buffer whole", _ {
		Captured c;
		const std::string what(700, 'x');
		c.report.fail("%s", what.c_str());
		expect(c.text()).to_equal("SR-FAIL " + what + "\n");
	});

	it("counts failures printed elsewhere without printing", _ {
		Captured c;
		c.report.count();
		c.report.count(3);
		expect(c.text()).to_equal(std::string{});
		expect(c.report.failures()).to_equal(int32_t{4});
	});

	it("prints the result line with the count", _ {
		Captured c;
		c.report.fail("one");
		c.report.fail("two");
		c.report.printResult();
		auto out = expect(c.text());
		out.to(HaveLine(out, "SR-RESULT failures=2"));
		out.to(HaveLinesStartingWith(out, "SR-FAIL ", 2));
	});

	it("prints failures=0 for a clean run, and reset zeroes the count", _ {
		Captured c;
		c.report.fail("one");
		c.report.reset();
		c.report.printResult();
		expect(c.text()).to_equal(std::string{"SR-FAIL one\nSR-RESULT failures=0\n"});
		expect(c.report.clean()).to_be_true();
	});
	});

	context("Wait", _ {
	it("is met on a poll whose condition holds, with no failure", _ {
		Captured c;
		Wait wait{c.report, "the read", 3};
		expect(wait.poll(false)).to_equal(WaitState::Pending);
		expect(wait.poll(true)).to_equal(WaitState::Met);
		expect(wait.ticks()).to_equal(int32_t{1});
		expect(c.report.failures()).to_equal(int32_t{0});
		expect(c.text()).to_equal(std::string{});
	});

	it("expires on the unmet poll that reaches its budget, recording one failure that names it", _ {
		Captured c;
		Wait wait{c.report, "the staged open issuing its read", 3};
		expect(wait.poll(false)).to_equal(WaitState::Pending);
		expect(wait.poll(false)).to_equal(WaitState::Pending);
		expect(wait.poll(false)).to_equal(WaitState::Expired);
		expect(wait.ticks()).to_equal(int32_t{3});
		auto out = expect(c.text());
		out.to(HaveLine(out, "SR-FAIL the staged open issuing its read: not within 3 ticks"));
		out.to(HaveLinesStartingWith(out, "SR-FAIL ", 1));
		expect(c.report.failures()).to_equal(int32_t{1});
	});

	it("keeps its state once ended, and records nothing more", _ {
		Captured c;
		Wait expired{c.report, "a", 1};
		expect(expired.poll(false)).to_equal(WaitState::Expired);
		expect(expired.poll(true)).to_equal(WaitState::Expired);
		expect(expired.poll(false)).to_equal(WaitState::Expired);
		Wait met{c.report, "b", 1};
		expect(met.poll(true)).to_equal(WaitState::Met);
		expect(met.poll(false)).to_equal(WaitState::Met);
		auto out = expect(c.text());
		out.to(HaveLinesStartingWith(out, "SR-FAIL ", 1));
		expect(c.report.failures()).to_equal(int32_t{1});
	});

	it("reports done for met and expired alike, and not while pending", _ {
		Captured c;
		Wait wait{c.report, "a", 2};
		expect(wait.done(false)).to_be_false();
		expect(wait.done(false)).to_be_true();
		expect(wait.state()).to_equal(WaitState::Expired);
	});

	it("starts over on restart", _ {
		Captured c;
		Wait wait{c.report, "a", 2};
		wait.poll(false);
		wait.poll(true);
		wait.restart();
		expect(wait.state()).to_equal(WaitState::Pending);
		expect(wait.ticks()).to_equal(int32_t{0});
		expect(wait.budget()).to_equal(int32_t{2});
	});

	it("expires on its first unmet poll with a budget of one", _ {
		Captured c;
		Wait wait{c.report, "a", 1};
		expect(wait.poll(false)).to_equal(WaitState::Expired);
	});

	it("names its states when printed", _ {
		expect(named(WaitState::Pending)).to_equal(std::string{"Pending"});
		expect(named(WaitState::Met)).to_equal(std::string{"Met"});
		expect(named(WaitState::Expired)).to_equal(std::string{"Expired"});
	});
	});
});
// clang-format on

CPPSPEC_SPEC(scenario_report)
