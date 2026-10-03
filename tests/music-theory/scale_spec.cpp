#include "music-theory/musical_key.h"
#include "music-theory/note_set.h"
#include "music-theory/preset_scales.h"
#include "music-theory/scale_mapper.h"

#include "mock_panic.h"

#include <cppspec.hpp>

using namespace deluge::music;

// clang-format off
describe scale("NoteSet, the preset scales, MusicalKey and ScaleMapper", ${
	it("holds the notes it is given", _ {
		NoteSet major{0, 2, 4, 5, 7, 9, 11};
		expect(static_cast<int>(major.count())).to_equal(7);
		expect(major.has(4)).to_equal(true);
		expect(major.has(3)).to_equal(false);
	});

	it("names the major preset as the scale of the major note set", _ {
		expect(getScale(NoteSet{0, 2, 4, 5, 7, 9, 11})).to_equal(MAJOR_SCALE);
	});

	it("places a note by its interval above the key's root", _ {
		MusicalKey key;
		key.rootNote = 9;
		expect(static_cast<int>(key.intervalOf(9 + 12 + 4))).to_equal(4);
		expect(static_cast<int>(key.intervalOf(9 - 1))).to_equal(11);
	});

	it("maps the notes in use from major to pentatonic", _ {
		ScaleMapper mapper;
		ScaleChange changes;
		NoteSet major{0, 2, 4, 5, 7, 9, 11};
		NoteSet pentatonic{0, 2, 4, 7, 9};
		expect(mapper.computeChangeFrom(NoteSet{0, 4, 7}, major, pentatonic, changes)).to_equal(true);
	});

	it("refuses, freezing with SM01, when the notes in use do not fit the target scale", _ {
		// Five notes, but the root is always implied: transposing another note onto the root maps many to one.
		ScaleMapper mapper;
		ScaleChange changes;
		NoteSet major{0, 2, 4, 5, 7, 9, 11};
		NoteSet pentatonic{0, 2, 4, 7, 9};
		deluge::test::clear_freeze();
		expect(mapper.computeChangeFrom(NoteSet{2, 4, 7, 9, 11}, major, pentatonic, changes)).to_equal(false);
		expect(deluge::test::last_freeze()).to_equal(std::string{"SM01"});
	});

	it("refuses, freezing with SM01, when a note in use is not in the source scale", _ {
		ScaleMapper mapper;
		ScaleChange changes;
		NoteSet major{0, 2, 4, 5, 7, 9, 11};
		NoteSet pentatonic{0, 2, 4, 7, 9};
		deluge::test::clear_freeze();
		expect(mapper.computeChangeFrom(NoteSet{3}, major, pentatonic, changes)).to_equal(false);
		expect(deluge::test::last_freeze()).to_equal(std::string{"SM01"});
	});
});

CPPSPEC_SPEC(scale)
