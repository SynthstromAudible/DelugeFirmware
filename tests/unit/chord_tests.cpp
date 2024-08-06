#include "CppUTest/TestHarness.h"
#include "gui/ui/keyboard/chords.h" // Include the header file for ChordList and related classes
#include "definitions_cxx.hpp"

using deluge::gui::ui::keyboard::ChordList;
using deluge::gui::ui::keyboard::Voicing;
using deluge::gui::ui::keyboard::NONE;

TEST_GROUP(ChordTests) {
    ChordList chordList;
};

TEST(ChordTests, getVoicingBoundsCheck) {
	// For each chord, iterate through all voicing offsets and then some
	for (int chordNo = 0; chordNo < kUniqueChords; chordNo++) {
		// iterate through -5 to twice the number of possible voicings to check bounds
		for (int voicingOffset = -5; voicingOffset < 2*kUniqueVoicings; voicingOffset++) {
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
			CHECK(valid);
		}
	}
}
