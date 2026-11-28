#include "ui/views/oled/arranger.h"
#include "ui/views/oled/detail/layout.h"
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

ArrangerResting resting(std::span<const LaneSpan> spans) {
	return {.song = "SONG 07",
	        .position = "17.3",
	        .lanes = 8,
	        .units = 64,
	        .spans = spans,
	        .window_start = 16,
	        .window_len = 16,
	        .playhead = 21};
}

constexpr std::array<LaneSpan, 4> kHeldLane{
    {{0, 0, 8, false}, {0, 16, 16, false, true}, {0, 40, 8, false}, {0, 52, 12, false}}};

constexpr ArrangerClipHeld kHeld{
    .name = "LEAD 2", .position = "BAR 17", .length = "8 BARS", .repeats = "X2", .units = 64, .lane = kHeldLane};

} // namespace

// clang-format off
describe arranger_resting_v("ui::views::oled::arranger_resting", $ {
	it("draws the title and window with no spans", _ {
		uitest::Rig48 r;
		arranger_resting(r.frame, resting({}));
		expect(lit_in(r, 0, 0, 128, r.style.title_height)).to_be_greater_than(0);
		const int32_t top = detail::row_y(r.frame, 0);
		expect(r.lit(32, top + kTop)).to_be_true();  // the window's top-left dot, bar 16 of 64
		expect(lit_in(r, 33, top + 1, 30, 20)).to_be_greater_than(0); // the playhead inside it
	});

	it("keeps the overview off the last two rows", _ {
		uitest::Rig48 r;
		arranger_resting(r.frame, resting(kSong));
		expect(lit_in(r, 0, 41, 128, 2)).to_equal(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		arranger_resting(r48.frame, resting(kSong));
		expect_golden(self, "ui/views/oled/arranger_resting_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		arranger_resting(r64.frame, resting(kSong));
		expect_golden(self, "ui/views/oled/arranger_resting_64.pbm", r64.pbm());
	});
});

describe arranger_clip_held_v("ui::views::oled::arranger_clip_held", $ {
	it("fills the selected instance and outlines the others", _ {
		uitest::Rig48 r;
		arranger_clip_held(r.frame, kHeld);
		expect(r.lit(47, 32 + kTop)).to_be_true();  // inside bars 16..32: filled
		expect(r.lit(87, 32 + kTop)).to_be_false(); // inside bars 40..48: outlined
		expect(r.lit(80, 32 + kTop)).to_be_true();  // its left edge
	});

	it("draws the text with an empty lane", _ {
		uitest::Rig48 r;
		ArrangerClipHeld d = kHeld;
		d.lane = {};
		arranger_clip_held(r.frame, d);
		const auto b = detail::body_rect(r.frame, 1);
		expect(lit_in(r, b.x, b.y, b.w, b.h)).to_equal(0);
		expect(lit_in(r, 0, detail::row_y(r.frame, 0), 128, r.style.row_height)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		arranger_clip_held(r48.frame, kHeld);
		expect_golden(self, "ui/views/oled/arranger_clip_held_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		arranger_clip_held(r64.frame, kHeld);
		expect_golden(self, "ui/views/oled/arranger_clip_held_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(arranger_spec, arranger_resting_v, arranger_clip_held_v)
