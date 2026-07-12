#include "CppUTest/TestHarness.h"
#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/notes_state.h"

using deluge::gui::ui::keyboard::kHighestKeyboardNote;
using deluge::gui::ui::keyboard::NotesState;

TEST_GROUP(NotesStateTests){};

// Regression for the const iterator: end() must be begin() + count, not
// notes.end() + count (which walks far past the array).
TEST(NotesStateTests, constIterationVisitsExactlyCountNotes) {
	NotesState state;
	state.enableNote(10, 64);
	state.enableNote(20, 64);
	state.enableNote(30, 64);

	const NotesState& constState = state;
	size_t visited = 0;
	for (auto it = constState.begin(); it != constState.end(); ++it) {
		++visited;
	}
	CHECK_EQUAL(3, visited);
}

TEST(NotesStateTests, enableNoteTruncatesAtMaxWithoutOverflowingIndex) {
	NotesState state;
	// Enable one more than the maximum number of simultaneous notes.
	for (uint8_t note = 0; note < kMaxNumActiveNotes + 1; ++note) {
		auto idx = state.enableNote(note, 64);
		// The returned index must always be a valid slot the caller can index,
		// or equal to count (the documented "not added" sentinel) - never past it.
		CHECK(idx <= kMaxNumActiveNotes);
	}
	CHECK_EQUAL(kMaxNumActiveNotes, state.count);
	// The overflow note must not have been recorded.
	CHECK_FALSE(state.noteEnabled(kMaxNumActiveNotes));
}

TEST(NotesStateTests, enableNoteDedupIncrementsActivationCount) {
	NotesState state;
	auto first = state.enableNote(42, 64);
	auto second = state.enableNote(42, 64);
	CHECK_EQUAL(first, second);
	CHECK_EQUAL(1, state.count);
	CHECK_EQUAL(1, state.notes[first].activationCount);
}

TEST(NotesStateTests, outOfRangeNoteIsIgnored) {
	NotesState state;
	state.enableNote(kHighestKeyboardNote, 64); // out of range, must be ignored
	CHECK_EQUAL(0, state.count);
	CHECK_FALSE(state.noteEnabled(kHighestKeyboardNote));
}
