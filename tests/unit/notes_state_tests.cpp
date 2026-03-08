#include "CppUTest/TestHarness.h"
#include "gui/ui/keyboard/notes_state.h"

using namespace deluge::gui::ui::keyboard;

namespace {

TEST_GROUP(NotesState){};

// const end() must use begin()+count, not end()+count
TEST(NotesState, constIteratorRange) {
	NotesState ns;
	ns.enableNote(60, 100);
	ns.enableNote(64, 80);

	const NotesState& cns = ns;
	int count = 0;
	for (auto it = cns.begin(); it != cns.end(); ++it) {
		count++;
	}
	CHECK_EQUAL(2, count);
}

TEST(NotesState, emptyIteratorRange) {
	const NotesState ns;
	CHECK(ns.begin() == ns.end());
}

TEST(NotesState, mutableIteratorRange) {
	NotesState ns;
	ns.enableNote(48, 127);
	ns.enableNote(55, 90);
	ns.enableNote(60, 70);

	int count = 0;
	for (auto& note : ns) {
		CHECK(note.velocity > 0);
		count++;
	}
	CHECK_EQUAL(3, count);
}

TEST(NotesState, rangeForConst) {
	NotesState ns;
	ns.enableNote(36, 100);

	const NotesState& cns = ns;
	int count = 0;
	for (const auto& note : cns) {
		CHECK_EQUAL(36, note.note);
		count++;
	}
	CHECK_EQUAL(1, count);
}

TEST(NotesState, enableDuplicateIncrementsActivation) {
	NotesState ns;
	ns.enableNote(60, 100);
	ns.enableNote(60, 80);

	CHECK_EQUAL(1, ns.count);

	const NotesState& cns = ns;
	int count = 0;
	for (auto it = cns.begin(); it != cns.end(); ++it) {
		count++;
	}
	CHECK_EQUAL(1, count);
}

} // namespace
