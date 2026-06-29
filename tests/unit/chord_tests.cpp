#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/chords.h"

using deluge::gui::ui::keyboard::ChordList;
using deluge::gui::ui::keyboard::ChordSelection;
using deluge::gui::ui::keyboard::MAJ3;
using deluge::gui::ui::keyboard::NONE;
using deluge::gui::ui::keyboard::P5;
using deluge::gui::ui::keyboard::resolveChordNotes;
using deluge::gui::ui::keyboard::ROOT;
using deluge::gui::ui::keyboard::Voicing;

TEST_GROUP(ChordTests) {
	ChordList chordList;
};

namespace {
// A C major triad voicing with all unused slots explicitly NONE.
Voicing majorTriadVoicing() {
	Voicing v{};
	v.offsets[0] = ROOT;
	v.offsets[1] = MAJ3;
	v.offsets[2] = P5;
	for (int i = 3; i < kMaxChordKeyboardSize; i++) {
		v.offsets[i] = NONE;
	}
	return v;
}
} // namespace

TEST(ChordTests, resolveChordNotesMajorTriad) {
	ChordSelection sel{.rootNote = 60, .voicing = majorTriadVoicing()};
	int16_t notes[kMaxChordKeyboardSize];
	uint8_t n = resolveChordNotes(sel, notes, kMaxChordKeyboardSize);

	CHECK_EQUAL(3, n);
	CHECK_EQUAL(60, notes[0]); // C
	CHECK_EQUAL(64, notes[1]); // E
	CHECK_EQUAL(67, notes[2]); // G
}

TEST(ChordTests, resolveChordNotesSkipsNoneHoles) {
	// A NONE in the middle of the offsets array must be skipped, not terminate the chord.
	Voicing v{};
	v.offsets[0] = ROOT;
	v.offsets[1] = NONE;
	v.offsets[2] = P5;
	for (int i = 3; i < kMaxChordKeyboardSize; i++) {
		v.offsets[i] = NONE;
	}
	ChordSelection sel{.rootNote = 48, .voicing = v};
	int16_t notes[kMaxChordKeyboardSize];
	uint8_t n = resolveChordNotes(sel, notes, kMaxChordKeyboardSize);

	CHECK_EQUAL(2, n);
	CHECK_EQUAL(48, notes[0]);
	CHECK_EQUAL(55, notes[1]);
}

TEST(ChordTests, resolveChordNotesRespectsMaxNotes) {
	ChordSelection sel{.rootNote = 60, .voicing = majorTriadVoicing()};
	int16_t notes[kMaxChordKeyboardSize];
	uint8_t n = resolveChordNotes(sel, notes, 2); // cap below the 3 available

	CHECK_EQUAL(2, n);
	CHECK_EQUAL(60, notes[0]);
	CHECK_EQUAL(64, notes[1]);
}

TEST(ChordTests, getChordBoundsCheck) {
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
			CHECK(valid);
		}
	}
}

TEST(ChordTests, adjustChordRowOffsetBoundsCheck) {
	// Test that the chord row offset is bounded by 0 and kOffScreenChords
	// Test the lower bound
	chordList.chordRowOffset = 0;
	chordList.adjustChordRowOffset(-1);
	CHECK_EQUAL(0, chordList.chordRowOffset);

	// Test the upper bound
	chordList.chordRowOffset = kOffScreenChords;
	chordList.adjustChordRowOffset(1);
	CHECK_EQUAL(kOffScreenChords, chordList.chordRowOffset);

	// Test that doing a 0 offset doesn't change the value
	chordList.chordRowOffset = kUniqueChords / 2;
	chordList.adjustChordRowOffset(0);
	CHECK_EQUAL(kUniqueChords / 2, chordList.chordRowOffset);

	// Test that a 1 offset increases the value by 1
	chordList.chordRowOffset = 0;
	chordList.adjustChordRowOffset(1);
	CHECK_EQUAL(1, chordList.chordRowOffset);

	// Test that a -1 offset decreases the value by 1
	chordList.chordRowOffset = kOffScreenChords;
	chordList.adjustChordRowOffset(-1);
	CHECK_EQUAL(kOffScreenChords - 1, chordList.chordRowOffset);
}

TEST(ChordTests, adjustVoicingOffsetBoundsCheck) {
	for (int chordNo = 0; chordNo < kUniqueChords; chordNo++) {
		// Test that the voicing offset is bounded by 0 and kUniqueVoicings - 1
		// Test the lower bound
		chordList.voicingOffset[chordNo] = 0;
		chordList.adjustVoicingOffset(chordNo, -1);
		CHECK_EQUAL(0, chordList.voicingOffset[chordNo]);

		// Test the upper bound
		chordList.voicingOffset[chordNo] = kUniqueVoicings - 1;
		chordList.adjustVoicingOffset(chordNo, 1);
		CHECK_EQUAL(kUniqueVoicings - 1, chordList.voicingOffset[chordNo]);

		// Test that doing a 0 offset doesn't change the value
		chordList.voicingOffset[chordNo] = kUniqueVoicings / 2;
		chordList.adjustVoicingOffset(chordNo, 0);
		CHECK_EQUAL(kUniqueVoicings / 2, chordList.voicingOffset[chordNo]);

		// Test that a 1 offset increases the value by 1
		chordList.voicingOffset[chordNo] = 0;
		chordList.adjustVoicingOffset(chordNo, 1);
		CHECK_EQUAL(1, chordList.voicingOffset[chordNo]);

		// Test that a -1 offset decreases the value by 1
		chordList.voicingOffset[chordNo] = kUniqueVoicings - 1;
		chordList.adjustVoicingOffset(chordNo, -1);
		CHECK_EQUAL(kUniqueVoicings - 2, chordList.voicingOffset[chordNo]);
	}
}
