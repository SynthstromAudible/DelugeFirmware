#include "music-theory/note_length.h"

#include <cppspec.hpp>

using namespace deluge::music;

// clang-format off
describe note_length("note-length names", ${
	it("names divisions with their ordinal suffix and bars by count", _ {
		expect(note_length_name(-4, "-notes", false).view()).to_equal(std::string_view{"16th-notes"});
		expect(note_length_name(-1, "", false).view()).to_equal(std::string_view{"2nd"});
		expect(note_length_name(1, "", false).view()).to_equal(std::string_view{"2-bar"});
		expect(note_length_name(0, "", true).view()).to_equal(std::string_view{"1 bar (per column)"});
		expect(note_length_name(2, "", true).view()).to_equal(std::string_view{"4 bars (per column)"});
	});
	it("finds a note's magnitude against the tick magnitude", _ {
		expect(note_magnitude(3, 0)).to_equal(-5);
		expect(note_magnitude(6, 0)).to_equal(-4);
		expect(note_magnitude(3, 1)).to_equal(-6);
	});
	it("names sync values in both forms", _ {
		expect(sync_value_name(5, 3, SyncNameForm::FULL).view()).to_equal(std::string_view{"16th-notes"});
		expect(sync_value_name(14, 3, SyncNameForm::FULL).view()).to_equal(std::string_view{"16th-tplts"});
		expect(sync_value_name(5, 3, SyncNameForm::HORIZONTAL_LABEL).view()).to_equal(std::string_view{"16th"});
	});
	it("names a bar triplet or dotted with its suffix once, and a bar label without the dash", _ {
		expect(sync_value_name(10, 2, SyncNameForm::FULL).view()).to_equal(std::string_view{"2-bar-tplts"});
		expect(sync_value_name(19, 2, SyncNameForm::FULL).view()).to_equal(std::string_view{"2-bar-dtted"});
		expect(sync_value_name(1, 2, SyncNameForm::HORIZONTAL_LABEL).view()).to_equal(std::string_view{"2bar"});
	});
	it("names value 0 as an even length without a suffix", _ {
		expect(sync_value_name(0, 3, SyncNameForm::FULL).view()).to_equal(std::string_view{"2-bar"});
	});
	it("holds the longest names", _ {
		expect(note_length_name(-31, "-notes", false).view()).to_equal(std::string_view{"2147483648th-notes"});
		expect(note_length_name(31, "", true).view()).to_equal(std::string_view{"2147483648 bars (per column)"});
	});
	it("ends for the longest note length", _ {
		expect(note_magnitude(0xFFFFFFFFu, 0)).to_equal(26);
	});
});

CPPSPEC_SPEC(note_length)
