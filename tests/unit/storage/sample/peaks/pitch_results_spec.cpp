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

#include "model/sample/pitch_results.h"

#include "cppspec.hpp"

#include <vector>

// A sample's remembered pitches: one per search, round-tripped through the sidecar's PTCH section.

using deluge::sample::PitchLimits;
using deluge::sample::PitchResults;

namespace {
constexpr PitchLimits kDefault{.minFreqHz = 20, .maxFreqHz = 10000, .primeTest = true};
constexpr PitchLimits kNarrow{.minFreqHz = 20, .maxFreqHz = 10000, .primeTest = false};
} // namespace

// clang-format off
describe pitch_results("a sample's remembered pitches", $ {
	it("answers the search it was remembered for, and no other", _ {
		PitchResults r;
		expect(r.empty()).to_equal(true);
		r.remember(kDefault, 440.0f);
		expect(r.find(kDefault).value_or(-1.0f)).to_equal(440.0f);
		expect(r.find(kNarrow).has_value()).to_equal(false);
	});
	it("replaces the oldest once full, and a repeat search in place", _ {
		PitchResults r;
		for (int32_t i = 0; i < 5; i++) {
			r.remember({.minFreqHz = static_cast<float>(10 + i), .maxFreqHz = 1000, .primeTest = true}, 100.0f + i);
		}
		expect(r.find({.minFreqHz = 10, .maxFreqHz = 1000, .primeTest = true}).has_value()).to_equal(false);
		expect(r.find({.minFreqHz = 14, .maxFreqHz = 1000, .primeTest = true}).value_or(-1.0f)).to_equal(104.0f);
		r.remember({.minFreqHz = 14, .maxFreqHz = 1000, .primeTest = true}, 7.0f);
		expect(r.find({.minFreqHz = 14, .maxFreqHz = 1000, .primeTest = true}).value_or(-1.0f)).to_equal(7.0f);
		expect(r.find({.minFreqHz = 11, .maxFreqHz = 1000, .primeTest = true}).value_or(-1.0f)).to_equal(101.0f);
	});
	it("round-trips through its section", _ {
		PitchResults r;
		r.remember(kDefault, 261.6f);
		r.remember(kNarrow, 0.0f);
		const std::span<const uint8_t> bytes = r.encode();
		expect(bytes.size()).to_equal(size_t{4 + 2 * 16});
		const std::optional<PitchResults> back = PitchResults::decode(bytes);
		expect(back.has_value()).to_equal(true);
		expect(back->find(kDefault).value_or(-1.0f)).to_equal(261.6f);
		expect(back->find(kNarrow).value_or(-1.0f)).to_equal(0.0f);
		expect(back->unwritten()).to_equal(false);
	});
	it("refuses a malformed section", _ {
		PitchResults r;
		r.remember(kDefault, 261.6f);
		std::vector<uint8_t> bytes(r.encode().begin(), r.encode().end());
		expect(PitchResults::decode(std::span<const uint8_t>{bytes}.first(bytes.size() - 1)).has_value()).to_equal(false);
		bytes[0] = 5;
		expect(PitchResults::decode(bytes).has_value()).to_equal(false);
		expect(PitchResults::decode({}).has_value()).to_equal(false);
	});
	it("is unwritten after a remember until marked written", _ {
		PitchResults r;
		expect(r.unwritten()).to_equal(false);
		r.remember(kDefault, 1.0f);
		expect(r.unwritten()).to_equal(true);
		r.markWritten();
		expect(r.unwritten()).to_equal(false);
	});
});
// clang-format on

CPPSPEC_SPEC(pitch_results)
