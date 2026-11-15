#include "util/name_compare.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

// clang-format off
describe name_compare_group("name compare", ${
	before_each([] {
		shouldInterpretNoteNames = false;
		octaveStartsFromA = false;
	});

	it("digit runs compare as numbers", _ {
		// The whole point of "special": plain strcmp would put SONG10 before SONG2.
		expect(strcmpspecial("SONG2.XML", "SONG10.XML")).to_be_less_than(0);
		expect(strcmpspecial("SONG10.XML", "SONG2.XML")).to_be_greater_than(0);
		expect(strcmpspecial("SONG185.XML", "SONG185.XML")).to_equal(0);
	});

	it("letter suffix sorts after bare number", _ {
		// Relied on by the subslot probe: SONG185A must sort after SONG185.
		expect(strcmpspecial("SONG185.XML", "SONG185A.XML")).to_be_less_than(0);
		expect(strcmpspecial("SONG185A.XML", "SONG185B.XML")).to_be_less_than(0);
	});

	it("prefix is order preserving", _ {
		// The invariant the SONG-prefixed names rely on: adding the constant "SONG" prefix to every
		// entry does not reorder them relative to each other.
		expect(strcmpspecial("1.XML", "2.XML")).to_be_less_than(0);
		expect(strcmpspecial("SONG1.XML", "SONG2.XML")).to_be_less_than(0);
		expect(strcmpspecial("2.XML", "10.XML")).to_be_less_than(0);
		expect(strcmpspecial("SONG2.XML", "SONG10.XML")).to_be_less_than(0);
	});
});

DELUGE_SPEC(name_compare_spec, name_compare_group)
