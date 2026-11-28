#include "ui/views/oled/session.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cstdint>

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;

namespace {

constexpr SessionResting kResting{.position = "17.3.1",
                                  .tempo = "120.0",
                                  .song = "SONG 07",
                                  .scale = "C MINOR",
                                  .swing = "SW 56%",
                                  .metronome = true,
                                  .level_l = 0.72f,
                                  .level_r = 0.66f};

constexpr SessionQueued kQueued{.position = "17.3.1",
                                .tempo = "120.0",
                                .next_name = "CHORUS",
                                .next_label = "NEXT",
                                .when = "IN 2 BARS",
                                .metronome = true,
                                .progress = 0.4f};

constexpr std::array<RowText, 3> kSectionRows{{{"REPEATS", "4"}, {"CLIPS", "3"}, {"PLAYED", "1 OF 4"}}};

ClipHeld clip_held_data() {
	return {.name = "LEAD 2",
	        .type = "SYNTH",
	        .length = "4 BARS",
	        .launch = "LAUNCH 1 BAR",
	        .length_ticks = kClipTicks,
	        .notes = kClip,
	        .low = kLow,
	        .high = kHigh};
}

} // namespace

// clang-format off
describe session_resting_v("ui::views::oled::session_resting", $ {
	it("runs the level lines along the last two rows to the level", _ {
		uitest::Rig48 r;
		SessionResting d = kResting;
		d.level_l = 0.5f;
		d.level_r = 0.25f;
		session_resting(r.frame, d);
		expect(r.lit(60, 41 + kTop)).to_be_true();   // L: solid to column 64
		expect(r.lit(61, 42 + kTop)).to_be_false();  // R: dotted past column 32 (odd columns dark)
		expect(r.lit(30, 42 + kTop)).to_be_true();   // R: solid to column 32
	});

	it("leaves the metronome out when it is off", _ {
		uitest::Rig48 r;
		SessionResting d = kResting;
		d.metronome = false;
		session_resting(r.frame, d);
		const int32_t x = 128 - 5 * 6 - 10;
		expect(lit_in(r, x, 3, 7, 8)).to_equal(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		session_resting(r48.frame, kResting);
		expect_golden(self, "ui/views/oled/session_resting_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		session_resting(r64.frame, kResting);
		expect_golden(self, "ui/views/oled/session_resting_64.pbm", r64.pbm());
	});
});

describe session_queued_v("ui::views::oled::session_queued", $ {
	it("clamps a progress past 1", _ {
		uitest::Rig48 a;
		uitest::Rig48 b;
		SessionQueued d = kQueued;
		d.progress = 3.f;
		session_queued(a.frame, d);
		d.progress = 1.f;
		session_queued(b.frame, d);
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		session_queued(r48.frame, kQueued);
		expect_golden(self, "ui/views/oled/session_queued_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		session_queued(r64.frame, kQueued);
		expect_golden(self, "ui/views/oled/session_queued_64.pbm", r64.pbm());
	});
});

describe clip_held_v("ui::views::oled::clip_held", $ {
	it("draws with no notes", _ {
		uitest::Rig48 r;
		ClipHeld d = clip_held_data();
		d.notes = {};
		clip_held(r.frame, d);
		expect(lit_in(r, 0, 0, 128, r.style.title_height)).to_be_greater_than(0);
		const auto b = detail::body_rect(r.frame, 1);
		expect(lit_in(r, b.x, b.y, b.w, b.h)).to_equal(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		clip_held(r48.frame, clip_held_data());
		expect_golden(self, "ui/views/oled/clip_held_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		clip_held(r64.frame, clip_held_data());
		expect_golden(self, "ui/views/oled/clip_held_64.pbm", r64.pbm());
	});
});

describe clip_held_short_v("ui::views::oled::clip_held on a short frame", $ {
	it("draws in a frame too short for its strip", _ {
		uitest::Rig48 r;
		r.frame.region({0, 0, 128, 20}, false, [&](widgets::oled::OledFrame& g) { clip_held(g, clip_held_data()); });
		expect(lit_in(r, 0, 20, 128, 23)).to_equal(0);
	});
});

describe section_held_v("ui::views::oled::section_held", $ {
	it("draws with empty text", _ {
		uitest::Rig48 r;
		section_held(r.frame, {.name = "", .position = "", .rows = {}});
		// the title's rule only
		expect(lit_in(r, 0, r.style.title_height + 1, 128, 43 - r.style.title_height - 1)).to_equal(0);
	});

	it("matches its goldens", _ {
		const SectionHeld d{.name = "SECTION B", .position = "2/6", .rows = kSectionRows};
		uitest::Rig48 r48;
		section_held(r48.frame, d);
		expect_golden(self, "ui/views/oled/section_held_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		section_held(r64.frame, d);
		expect_golden(self, "ui/views/oled/section_held_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(session_spec, session_resting_v, session_queued_v, clip_held_v, clip_held_short_v, section_held_v)
