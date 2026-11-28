#include "ui/views/oled/marker.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;

namespace {

constexpr MarkerPosition kStart{.label = "START", .loop_locked = false, .position = 44100, .sample_rate = 44100};
constexpr MarkerPosition kLoopLocked{.label = "LOOP START", .loop_locked = true, .position = 0, .sample_rate = 48000};

} // namespace

// clang-format off
describe marker_position_v("ui::views::oled::marker_position", $ {
	it("draws the badge only on a locked loop", _ {
		uitest::Rig48 plain;
		marker_position(plain.frame, kStart);
		MarkerPosition locked = kStart;
		locked.loop_locked = true;
		uitest::Rig48 badged;
		marker_position(badged.frame, locked);
		expect(badged.pbm() == plain.pbm()).to_equal(false);
		expect(lit_in(plain, 0, plain.style.title_height + 5, 128, 22)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 s48;
		marker_position(s48.frame, kStart);
		expect_golden(self, "ui/views/oled/marker_start_48.pbm", s48.pbm());
		uitest::Rig64 s64;
		marker_position(s64.frame, kStart);
		expect_golden(self, "ui/views/oled/marker_start_64.pbm", s64.pbm());
		uitest::Rig48 l48;
		marker_position(l48.frame, kLoopLocked);
		expect_golden(self, "ui/views/oled/marker_loop_locked_48.pbm", l48.pbm());
		uitest::Rig64 l64;
		marker_position(l64.frame, kLoopLocked);
		expect_golden(self, "ui/views/oled/marker_loop_locked_64.pbm", l64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(views_oled_marker_spec, marker_position_v)
