// clang-format off
#include "controls/knob_focus.h"
#include "deluge-project/song.h"
#include <cppspec.hpp>

using namespace deluge;

describe knob_focus("the gold knobs' focus", ${
	it("has no owner and mode 0 with no focus", _{
		const project::Song song{};
		const controls::KnobFocus focus{};
		expect(controls::knob_owner(focus).has_value()).to_be_false();
		expect(int{controls::knob_mode(song, focus)}).to_equal(0);
	});
	it("names the song as the owner of the song's knobs", _{
		const controls::KnobFocus focus{.owner = controls::SongFocus{}};
		expect(controls::knob_owner(focus)->owner == project::OwnerId::song()).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(knob_focus)
