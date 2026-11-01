#include "instruments/deluge/sound_state.h"

#include <cppspec.hpp>

using namespace deluge::instrument;

// clang-format off
describe sound_state("the Deluge instrument's state for a sound", ${
	it("compares equal field by field, ranges and DX7 patch included", _ {
		SoundState a;
		SoundState b;
		expect(a).to_equal(b);
		b.sources[1].ranges.push_back({.top_note = 60, .file_path = "SAMPLES/A.WAV"});
		expect(a).not_().to_equal(b);
		a.sources[1].ranges.push_back({.top_note = 60, .file_path = "SAMPLES/A.WAV"});
		a.sources[0].dx_patch.emplace();
		expect(a).not_().to_equal(b);
		b.sources[0].dx_patch.emplace();
		expect(a).to_equal(b);
	});
});

CPPSPEC_SPEC(sound_state)
