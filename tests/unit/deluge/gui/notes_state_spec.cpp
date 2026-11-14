#include "definitions_cxx.hpp"
#include "gui/ui/keyboard/notes_state.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::gui::ui::keyboard::kHighestKeyboardNote;
using deluge::gui::ui::keyboard::NotesState;

// clang-format off
describe notes_state_group("notes state", ${
	// The const iterator's end() must be begin() + count, not
	// notes.end() + count (which walks far past the array).
	it("const iteration visits exactly count notes", _ {
		NotesState state;
		state.enableNote(10, 64);
		state.enableNote(20, 64);
		state.enableNote(30, 64);

		const NotesState& constState = state;
		size_t visited = 0;
		for (auto iter = constState.begin(); iter != constState.end(); ++iter) {
			++visited;
		}
		expect(visited).to_equal(3);
	});

	it("enable note truncates at max without overflowing index", _ {
		NotesState state;
		// Enable one more than the maximum number of simultaneous notes.
		for (uint8_t note = 0; note < kMaxNumActiveNotes + 1; ++note) {
			auto idx = state.enableNote(note, 64);
			// The returned index must always be a valid slot the caller can index,
			// or equal to count (the documented "not added" sentinel) - never past it.
			expect(idx).not_().to_be_greater_than(kMaxNumActiveNotes);
		}
		expect(state.count).to_equal(kMaxNumActiveNotes);
		// The overflow note must not have been recorded.
		expect(state.noteEnabled(kMaxNumActiveNotes)).to_be_false();
	});

	it("enable note dedup increments activation count", _ {
		NotesState state;
		auto first = state.enableNote(42, 64);
		auto second = state.enableNote(42, 64);
		expect(second).to_equal(first);
		expect(state.count).to_equal(1);
		expect(state.notes[first].activationCount).to_equal(1);
	});

	it("out of range note is ignored", _ {
		NotesState state;
		state.enableNote(kHighestKeyboardNote, 64); // out of range, must be ignored
		expect(state.count).to_equal(0);
		expect(state.noteEnabled(kHighestKeyboardNote)).to_be_false();
	});
});

DELUGE_SPEC(notes_state_spec, notes_state_group)
