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

// main's envelope records (tools/envelope_records/): one per stimulus, a render per block, every stage one of main's
// seven, and each case that releases reaching its release.

#include "main_envelope_records.h"
#include "main_envelope_stimuli.h"

#include <algorithm>
#include <cppspec.hpp>
#include <cstdint>

#include "support/matchers/value_matchers.h"
#include "support/spec_main.h"

namespace me = deluge::test::main_envelope;

namespace {
constexpr uint8_t kRelease = 4;
constexpr uint8_t kOff = 6;
} // namespace

// clang-format off
describe main_envelope_records_spec_body("main's envelope records", ${
	it("holds one record per stimulus, one render per block", _ {
		expect(me::kRecords.size()).to_equal(me::kStimuli.size());
		for (size_t i = 0; i < me::kRecords.size(); ++i) {
			expect(me::kRecords[i].count).to_equal(me::kStimuli[i].blocks);
		}
	});

	it("names only main's stages", _ {
		for (const me::Record& r : me::kRecords) {
			for (uint32_t b = 0; b < r.count; ++b) {
				to_be_at_most(expect(r.stages[b]), kOff);
			}
		}
	});

	it("reaches a release or the end in every case with a note-off it honours and no later note", _ {
		for (size_t i = 0; i < me::kRecords.size(); ++i) {
			const me::Stimulus& s = me::kStimuli[i];
			const me::Record& r = me::kRecords[i];
			const bool honoured = s.mode != me::Mode::ADSR || s.has_sustain;
			const bool held_again = s.retrigger < s.blocks && s.retrigger > s.note_off; // a new note, never released
			if (s.note_off < s.blocks && honoured && !held_again) {
				to_be_at_least(expect(r.stages[r.count - 1]), kRelease);
			}
		}
	});
});
// clang-format on

DELUGE_SPEC(main_envelope_records_spec, main_envelope_records_spec_body)
