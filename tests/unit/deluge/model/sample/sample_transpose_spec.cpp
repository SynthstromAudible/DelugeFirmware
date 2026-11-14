#include "model/sample/sample_transpose.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::sample::SampleTranspose;
using deluge::sample::transposeForNote;

namespace {
void checkTranspose(auto& self, const SampleTranspose& t, int32_t semitones, int32_t cents) {
	expect(t.semitones).to_equal(semitones);
	expect(t.cents).to_equal(cents);
}
} // namespace

// clang-format off
describe sample_transpose_group("sample transpose", ${
	// A multisample range covering only key 58, its file detected 3 cents flat of it: snapped to exactly 58, so the file
	// plays in tune on the one key it covers. Folder import passes the key as the range's top note.
	it("one note range snaps small detune to its key", _ {
		checkTranspose(self, transposeForNote(57.97f, false, true, 58), 2, 0);
	});

	it("one note range keeps detune beyond four cents", _ {
		checkTranspose(self, transposeForNote(57.90f, false, true, 58), 2, 10);
	});

	it("one note range does not snap another key", _ {
		checkTranspose(self, transposeForNote(56.97f, false, true, 58), 3, 3);
	});

	it("range of several keys keeps its cents", _ {
		checkTranspose(self, transposeForNote(57.97f, false, false, 0), 2, 3);
	});

	// The only range of its source: the transpose folds into -5..6 semitones.
	it("only range minimizes octaves", _ {
		checkTranspose(self, transposeForNote(69.0f, true, false, 0), 3, 0); // A4: -9 -> 3
		checkTranspose(self, transposeForNote(45.0f, true, false, 0), 3, 0); // A2: 15 -> 3
		checkTranspose(self, transposeForNote(66.0f, true, false, 0), -6 + 12, 0);
	});

	it("exact note has no cents", _ {
		checkTranspose(self, transposeForNote(60.0f, false, false, 0), 0, 0);
	});
});

DELUGE_SPEC(sample_transpose_spec, sample_transpose_group)
