#include "ui/views/oled/detail/layout.h"
#include "ui/views/oled/keyboard.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <cstdint>

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;

namespace {

constexpr KeyboardPlaying kPlaying{.instrument = "LEAD 2", .scale = "C MIN", .chord = "CM7", .notes = "C3 EB3 G3 BB3"};
constexpr ScaleHeld kScale{.scale = "C MINOR", .count = "7", .mask = 0b0101'1010'1101, .root = 0};

} // namespace

// clang-format off
describe keyboard_playing_v("ui::views::oled::keyboard_playing", $ {
	it("draws with empty text", _ {
		uitest::Rig48 r;
		keyboard_playing(r.frame, {});
		expect(lit_count(r)).to_equal(0);
	});

	it("centres the notes on the last row", _ {
		uitest::Rig48 r;
		keyboard_playing(r.frame, kPlaying);
		const int32_t y = detail::last_row_y(r.frame);
		const int32_t left = lit_in(r, 0, y, 64, 8);
		const int32_t right = lit_in(r, 64, y, 64, 8);
		expect(left).to_be_greater_than(0);
		expect(right).to_be_greater_than(0);
		expect(lit_in(r, 0, y, 10, 8)).to_equal(0); // 13 characters (78 px) leave 25 px each side
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		keyboard_playing(r48.frame, kPlaying);
		expect_golden(self, "ui/views/oled/keyboard_playing_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		keyboard_playing(r64.frame, kPlaying);
		expect_golden(self, "ui/views/oled/keyboard_playing_64.pbm", r64.pbm());
	});
});

describe scale_held_v("ui::views::oled::scale_held", $ {
	it("keeps the octave one column in from each side", _ {
		uitest::Rig48 r;
		scale_held(r.frame, kScale);
		const int32_t top = detail::row_y(r.frame, 0);
		expect(lit_in(r, 0, top, 1, 43 - top)).to_equal(0);
		expect(lit_in(r, 127, top, 1, 43 - top)).to_equal(0);
		expect(lit_in(r, 1, top, 126, 43 - top)).to_be_greater_than(0);
	});

	it("draws an empty scale as outlines", _ {
		uitest::Rig48 r;
		ScaleHeld d = kScale;
		d.mask = 0;
		d.root = -1;
		scale_held(r.frame, d);
		expect(lit_count(r)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		scale_held(r48.frame, kScale);
		expect_golden(self, "ui/views/oled/scale_held_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		scale_held(r64.frame, kScale);
		expect_golden(self, "ui/views/oled/scale_held_64.pbm", r64.pbm());
	});
});

describe velocity_drums_v("ui::views::oled::velocity_drums", $ {
	it("clamps a velocity past 127", _ {
		uitest::Rig48 a;
		velocity_drums(a.frame, {.drum = "SNARE 2", .velocity = 300});
		uitest::Rig48 b;
		velocity_drums(b.frame, {.drum = "SNARE 2", .velocity = 127});
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("clamps a velocity below 0", _ {
		uitest::Rig48 a;
		velocity_drums(a.frame, {.drum = "SNARE 2", .velocity = -5});
		uitest::Rig48 b;
		velocity_drums(b.frame, {.drum = "SNARE 2", .velocity = 0});
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("reads no input", _ {
		uitest::Rig48 a;
		a.input.turn = 3;
		velocity_drums(a.frame, {.drum = "SNARE 2", .velocity = 96});
		uitest::Rig48 b;
		velocity_drums(b.frame, {.drum = "SNARE 2", .velocity = 96});
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("keeps its bar clear of the digits at 64 rows", _ {
		uitest::Rig64 r;
		velocity_drums(r.frame, {.drum = "SNARE 2", .velocity = 96});
		expect(lit_in(r, 0, 64 - 10, 128, 10)).to_be_greater_than(0); // the bar along the bottom
		expect(lit_in(r, 0, 64 - 12, 128, 2)).to_equal(0);            // a gap above it
		expect(lit_in(r, 0, 20, 128, 30)).to_be_greater_than(0);      // the digits
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		velocity_drums(r48.frame, {.drum = "SNARE 2", .velocity = 96});
		expect_golden(self, "ui/views/oled/velocity_drums_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		velocity_drums(r64.frame, {.drum = "SNARE 2", .velocity = 96});
		expect_golden(self, "ui/views/oled/velocity_drums_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(keyboard_spec, keyboard_playing_v, scale_held_v, velocity_drums_v)
