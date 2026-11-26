#include "support/frames.h"
#include "ui/gfx/oled/canvas.h"
#include "ui/widgets/oled/page_drawing.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cstdint>
#include <vector>

using namespace deluge::ui;
using namespace deluge::ui::widgets::oled;
using deluge::ui::gfx::oled::Point;
namespace uitest = deluge::ui::test;

namespace {

/// Every mark once, the way a page uses them: a curve, a polyline, guides, a fill and three handles.
template <typename Rig>
void draw_kit(Rig& r) {
	OledFrame& f = r.frame;
	const PageBody b = page_body(f);
	const LevelFn half_sine = [](float t, const void*) { return t < 0.5f ? t * 2.f : (1.f - t) * 2.f; };
	curve(f, b, static_cast<float>(b.left), 60.f, half_sine, nullptr);
	const std::array<Point, 3> steps{{{64, b.bottom}, {80, b.center_y()}, {100, b.top}}};
	polyline(f, steps);
	guide_v(f, b, 60, b.top);
	guide_v(f, b, 80, b.center_y());
	const std::array<int32_t, 1> skip{80};
	guide_h(f, b.center_y(), 0, b.right, skip);
	fill_line(f, {102, b.top}, {124, b.bottom}, b.bottom, 102, 124);
	Handles h{f};
	h.draw(32, b.top, false);
	h.draw(80, b.center_y(), true);
	h.draw(80, b.center_y(), false); // skipped: the focused handle stays on top
}

/// Lit pixels in local row y of the 48-row rig (canvas row y + 5), as a column list.
std::vector<int32_t> lit_in_row(uitest::Rig48& r, int32_t y) {
	std::vector<int32_t> xs;
	for (int32_t x = 0; x < 128; x++) {
		if (r.lit(x, y + 5)) {
			xs.push_back(x);
		}
	}
	return xs;
}

} // namespace

// clang-format off
describe page_drawing_w("ui::widgets::oled::page_drawing", $ {
	it("puts the body where main's EQ page draws, on both panels", _ {
		uitest::Rig48 r48;
		const PageBody b48 = page_body(r48.frame);
		expect(b48.left).to_equal(3);
		expect(b48.right).to_equal(124);
		expect(b48.top).to_equal(15);
		expect(b48.bottom).to_equal(37);
		expect(b48.center_y()).to_equal(26);
		uitest::Rig64 r64;
		const PageBody b64 = page_body(r64.frame);
		expect(b64.top).to_equal(15);
		expect(b64.bottom).to_equal(58);
		expect(b64.center_y()).to_equal(36);
	});

	it("maps a level to a row truncated as the EQ's integer lerp is", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		expect(b.y_at(0.f)).to_equal(37);
		expect(b.y_at(1.f)).to_equal(15);
		expect(b.y_at(0.5f)).to_equal(26);
		expect(b.y_at(0.3f)).to_equal(30); // 37 - 6.6, truncated
	});

	it("dots a vertical guide on the rows top-1+4k from the first at or below from_y-1", _ {
		uitest::Rig48 r;
		const PageBody b = page_body(r.frame);
		guide_v(r.frame, b, 10, b.top);
		std::vector<int32_t> rows;
		for (int32_t y = 0; y < 43; y++) {
			if (r.lit(10, y + 5)) {
				rows.push_back(y);
			}
		}
		expect(rows).to_equal(std::vector<int32_t>{14, 18, 22, 26, 30, 34, 38});
		uitest::Rig48 r2;
		guide_v(r2.frame, b, 10, 24);
		expect(r2.lit(10, 22 + 5)).to_be_false();
		expect(r2.lit(10, 26 + 5)).to_be_true();
	});

	it("dots a horizontal guide where x % 6 == 3, clear of the skipped columns", _ {
		uitest::Rig48 r;
		const std::array<int32_t, 1> skip{15};
		guide_h(r.frame, 20, 0, 30, skip);
		expect(lit_in_row(r, 20)).to_equal(std::vector<int32_t>{3, 9, 21, 27});
	});

	it("draws a handle as main's indicator: outline, cleared centre, the focused centre inverted", _ {
		uitest::Rig48 r;
		r.frame.invert({8, 18, 5, 5}); // ink under the handle, which it must clear
		Handles h{r.frame};
		h.draw(10, 20, false);
		expect(r.lit(10, 25)).to_be_false();
		expect(r.lit(8, 23)).to_be_true();
		uitest::Rig48 s;
		Handles hs{s.frame};
		hs.draw(10, 20, true);
		expect(s.lit(10, 25)).to_be_true();
		expect(s.lit(9, 24)).to_be_true();
		hs.draw(10, 20, false);
		expect(s.lit(10, 25)).to_be_true(); // the unfocused one at the same spot is skipped
	});

	it("fills below a line every third column, every third row of the panel", _ {
		uitest::Rig48 r;
		fill_line(r.frame, {0, 20}, {12, 20}, 30, 0, 12);
		// Canvas row = local + 5; a fill dot sits where canvas x % 3 == 0 and canvas y % 3 == 1, down to local 30.
		expect(r.lit(3, 23 + 5)).to_be_true();  // canvas 28
		expect(r.lit(3, 22 + 5)).to_be_false(); // canvas 27
		expect(r.lit(4, 23 + 5)).to_be_false(); // canvas x 4
		expect(r.lit(3, 35 + 5)).to_be_false(); // below fill_bottom
		expect(r.lit(13, 20 + 5)).to_be_false(); // past max_x, the line too
	});

	it("draws every mark as the house style, on both panels", _ {
		uitest::Rig48 r48;
		draw_kit(r48);
		expect_golden(self, "ui/widgets/page_kit_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		draw_kit(r64);
		expect_golden(self, "ui/widgets/page_kit_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(page_drawing_spec, page_drawing_w)
