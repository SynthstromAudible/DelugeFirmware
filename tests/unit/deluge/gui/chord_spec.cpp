#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/chords.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::gui::ui::keyboard::ChordList;
using deluge::gui::ui::keyboard::NONE;
using deluge::gui::ui::keyboard::Voicing;

namespace {

struct ChordTests {
	ChordList chordList;

	template <class Self>
	void getChordBoundsCheck(Self& self) {
		// For each chord, iterate through all voicing offsets and then some
		for (int chordNo = 0; chordNo < kUniqueChords; chordNo++) {
			// iterate through -5 to twice the number of possible voicings to check bounds
			for (int voicingOffset = -5; voicingOffset < 2 * kUniqueVoicings; voicingOffset++) {
				// Set the voicing offset, even if it's out of bounds
				chordList.voicingOffset[chordNo] = voicingOffset;
				// Get the voicing, should return between voicing 0 and last valid voicing
				Voicing voicing = chordList.getChordVoicing(chordNo);

				// Check that the voicing is a valid voicing (at least one offset is not NONE)
				bool valid = false;
				for (int i = 0; i < kMaxChordKeyboardSize; i++) {
					int32_t offset = voicing.offsets[i];
					if (offset != NONE) {
						valid = true;
					}
				}
				expect(valid).to_be_true();
			}
		}
	}

	template <class Self>
	void adjustChordRowOffsetBoundsCheck(Self& self) {
		// Test that the chord row offset is bounded by 0 and kOffScreenChords
		// Test the lower bound
		chordList.chordRowOffset = 0;
		chordList.adjustChordRowOffset(-1);
		expect(chordList.chordRowOffset).to_equal(0);

		// Test the upper bound
		chordList.chordRowOffset = kOffScreenChords;
		chordList.adjustChordRowOffset(1);
		expect(chordList.chordRowOffset).to_equal(kOffScreenChords);

		// Test that doing a 0 offset doesn't change the value
		chordList.chordRowOffset = kUniqueChords / 2;
		chordList.adjustChordRowOffset(0);
		expect(chordList.chordRowOffset).to_equal(kUniqueChords / 2);

		// Test that a 1 offset increases the value by 1
		chordList.chordRowOffset = 0;
		chordList.adjustChordRowOffset(1);
		expect(chordList.chordRowOffset).to_equal(1);

		// Test that a -1 offset decreases the value by 1
		chordList.chordRowOffset = kOffScreenChords;
		chordList.adjustChordRowOffset(-1);
		expect(chordList.chordRowOffset).to_equal(kOffScreenChords - 1);
	}

	template <class Self>
	void adjustVoicingOffsetBoundsCheck(Self& self) {
		for (int chordNo = 0; chordNo < kUniqueChords; chordNo++) {
			// Test that the voicing offset is bounded by 0 and kUniqueVoicings - 1
			// Test the lower bound
			chordList.voicingOffset[chordNo] = 0;
			chordList.adjustVoicingOffset(chordNo, -1);
			expect(chordList.voicingOffset[chordNo]).to_equal(0);

			// Test the upper bound
			chordList.voicingOffset[chordNo] = kUniqueVoicings - 1;
			chordList.adjustVoicingOffset(chordNo, 1);
			expect(chordList.voicingOffset[chordNo]).to_equal(kUniqueVoicings - 1);

			// Test that doing a 0 offset doesn't change the value
			chordList.voicingOffset[chordNo] = kUniqueVoicings / 2;
			chordList.adjustVoicingOffset(chordNo, 0);
			expect(chordList.voicingOffset[chordNo]).to_equal(kUniqueVoicings / 2);

			// Test that a 1 offset increases the value by 1
			chordList.voicingOffset[chordNo] = 0;
			chordList.adjustVoicingOffset(chordNo, 1);
			expect(chordList.voicingOffset[chordNo]).to_equal(1);

			// Test that a -1 offset decreases the value by 1
			chordList.voicingOffset[chordNo] = kUniqueVoicings - 1;
			chordList.adjustVoicingOffset(chordNo, -1);
			expect(chordList.voicingOffset[chordNo]).to_equal(kUniqueVoicings - 2);
		}
	}
};
} // namespace

// clang-format off
describe chord_group("chord", ${
	it("get chord bounds check", _ { ChordTests{}.getChordBoundsCheck(self); });

	it("adjust chord row offset bounds check", _ { ChordTests{}.adjustChordRowOffsetBoundsCheck(self); });

	it("adjust voicing offset bounds check", _ { ChordTests{}.adjustVoicingOffsetBoundsCheck(self); });
});

DELUGE_SPEC(chord_spec, chord_group)
