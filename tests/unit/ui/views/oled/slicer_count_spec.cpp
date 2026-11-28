#include "ui/views/oled/slicer_count.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;

namespace {

constexpr SlicerCount kSixteen{.label = "NUM SLICES", .digits = "16"};
constexpr SlicerCount kTwo{.label = "NUM SLICES", .digits = "2"};

} // namespace

// clang-format off
describe slicer_count_v("ui::views::oled::slicer_count", $ {
	it("draws the count it is given", _ {
		uitest::Rig48 sixteen;
		slicer_count(sixteen.frame, kSixteen);
		uitest::Rig48 two;
		slicer_count(two.frame, kTwo);
		expect(two.pbm() == sixteen.pbm()).to_equal(false);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		slicer_count(r48.frame, kSixteen);
		expect_golden(self, "ui/views/oled/slicer_count_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		slicer_count(r64.frame, kSixteen);
		expect_golden(self, "ui/views/oled/slicer_count_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(slicer_count_spec, slicer_count_v)
