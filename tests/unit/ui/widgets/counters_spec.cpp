#include "support/frames.h"
#include "ui/widgets/oled/counters.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <cstdint>

using namespace deluge::ui;
using namespace deluge::ui::widgets::oled;
using deluge::ui::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

namespace {

constexpr int32_t kTop = 5; // the 48-row rig's hidden rows: canvas row = local row + 5

int32_t lit_count(uitest::Rig48& r) {
	int32_t n = 0;
	for (int32_t y = 0; y < 48; y++) {
		for (int32_t x = 0; x < 128; x++) {
			n += r.lit(x, y) ? 1 : 0;
		}
	}
	return n;
}

} // namespace

// clang-format off
describe amount_line_w("ui::widgets::oled::amount_line", $ {
	it("draws one line solid to the amount, then dotted", _ {
		uitest::Rig48 r;
		amount_line(r.frame, 20, 0, 100, 0.5f);
		for (int32_t x = 0; x <= 50; x++) {
			expect(r.lit(x, 20 + kTop)).to_be_true();
		}
		expect(r.lit(51, 20 + kTop)).to_be_false();
		expect(r.lit(52, 20 + kTop)).to_be_true();
		expect(r.lit(53, 20 + kTop)).to_be_false();
		expect(r.lit(100, 20 + kTop)).to_be_true();
	});

	it("draws a full amount solid to the end, and clamps", _ {
		uitest::Rig48 r;
		amount_line(r.frame, 20, 0, 100, 4.f);
		for (int32_t x = 0; x <= 100; x++) {
			expect(r.lit(x, 20 + kTop)).to_be_true();
		}
		expect(r.lit(101, 20 + kTop)).to_be_false();
	});
});

describe level_lines_w("ui::widgets::oled::level_lines", $ {
	it("draws L on the first row and R on the second", _ {
		uitest::Rig48 r;
		level_lines(r.frame, {0, 40, 128, 2}, 1.f, 0.f);
		expect(r.lit(127, 40 + kTop)).to_be_true();
		expect(r.lit(1, 41 + kTop)).to_be_false();
		expect(r.lit(2, 41 + kTop)).to_be_true();
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		level_lines(r48.frame, {0, 41, 128, 2}, 0.72f, 0.66f);
		expect_golden(self, "ui/widgets/level_lines_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		level_lines(r64.frame, {0, 62, 128, 2}, 0.72f, 0.66f);
		expect_golden(self, "ui/widgets/level_lines_64.pbm", r64.pbm());
	});
});

describe beat_boxes_w("ui::widgets::oled::beat_boxes", $ {
	it("fills the first boxes and outlines the rest", _ {
		uitest::Rig48 r;
		beat_boxes(r.frame, {0, 37, 128, 5}, 4, 1);
		// 4 boxes of 8 with gaps of 5: 47 wide, from x = (128 - 47) / 2 = 40
		expect(r.lit(44, 39 + kTop)).to_be_true();  // box 0's middle: filled
		expect(r.lit(57, 39 + kTop)).to_be_false(); // box 1's middle: outlined
		expect(r.lit(53, 39 + kTop)).to_be_true();  // box 1's left edge
	});

	it("sizes the boxes and gaps as given", _ {
		uitest::Rig48 r;
		beat_boxes(r.frame, {0, 37, 128, 5}, 16, 16, 6, 2);
		// 16 boxes of 6 with gaps of 2: 126 wide, from x = 1
		expect(r.lit(1, 39 + kTop)).to_be_true();
		expect(r.lit(7, 39 + kTop)).to_be_false();
		expect(r.lit(126, 39 + kTop)).to_be_true();
		expect(r.lit(0, 39 + kTop)).to_be_false();
	});

	it("draws nothing for zero boxes", _ {
		uitest::Rig48 r;
		beat_boxes(r.frame, {0, 37, 128, 5}, 0, 3);
		expect(lit_count(r)).to_equal(0);
	});

	it("clamps filled to the box count", _ {
		uitest::Rig48 over;
		uitest::Rig48 all;
		beat_boxes(over.frame, {0, 37, 128, 5}, 4, 9);
		beat_boxes(all.frame, {0, 37, 128, 5}, 4, 4);
		expect(over.pbm()).to_equal(all.pbm());
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		beat_boxes(r48.frame, {0, 37, 128, 5}, 4, 1);
		expect_golden(self, "ui/widgets/beat_boxes_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		beat_boxes(r64.frame, {0, 54, 128, 7}, 4, 1);
		expect_golden(self, "ui/widgets/beat_boxes_64.pbm", r64.pbm());
	});
});

describe position_bar_w("ui::widgets::oled::position_bar", $ {
	it("draws the window solid on a dotted track", _ {
		uitest::Rig48 r;
		position_bar(r.frame, {0, 36, 128, 5}, 64, 16, 16);
		expect(r.lit(32, 36 + kTop)).to_be_true();  // the window: 32..63, full height
		expect(r.lit(63, 40 + kTop)).to_be_true();
		expect(r.lit(64, 36 + kTop)).to_be_false();
		expect(r.lit(0, 38 + kTop)).to_be_true();   // the track: the middle row, every 2nd column
		expect(r.lit(1, 38 + kTop)).to_be_false();
	});

	it("fills the track when the window covers the whole", _ {
		uitest::Rig48 r;
		position_bar(r.frame, {0, 36, 128, 5}, 64, 0, 80);
		expect(r.lit(0, 36 + kTop)).to_be_true();
		expect(r.lit(127, 40 + kTop)).to_be_true();
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		position_bar(r48.frame, {0, 36, 128, 5}, 64, 16, 16);
		expect_golden(self, "ui/widgets/position_bar_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		position_bar(r64.frame, {0, 54, 128, 7}, 64, 16, 16);
		expect_golden(self, "ui/widgets/position_bar_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(counters_spec, amount_line_w, level_lines_w, beat_boxes_w, position_bar_w)
