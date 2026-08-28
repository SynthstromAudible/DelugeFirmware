#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "gfx/grid/rect.h"
#include "ui/core/metrics.h"

using deluge::gfx::grid::Rect;

// clang-format off
describe grid_rect("gfx::grid::Rect", $ {
	it("contains and intersects in pad units", _ {
		Rect r{2, 1, 4, 3};
		expect(r.contains(2, 1)).to_be_true();
		expect(r.contains(6, 1)).to_be_false();
		expect(r.intersect(Rect{4, 2, 10, 10})).to_equal(Rect{4, 2, 2, 2});
		expect(deluge::ui::kTextSpacingY).to_equal(9);
	});
	// split_rows(at) / split_cols(at): the same two-way-at-a-position shape as
	// gfx::oled::Rect::split_rows/split_cols and the SDK's Frame::split_rows/split_cols.
	it("splits rows into (top, bottom) at a row position, without gaps or overlap", _ {
		Rect r{5, 10, 4, 8}; // x=5, y=10, w=4, h=8
		auto [top, bottom] = r.split_rows(3);
		expect(top).to_equal(Rect{5, 10, 4, 3});
		expect(bottom).to_equal(Rect{5, 13, 4, 5});
		expect(top.h + bottom.h).to_equal(r.h);
	});
	it("splits columns into (left, right) at a column position, transposed from split_rows", _ {
		Rect r{10, 5, 8, 4};
		auto [left, right] = r.split_cols(3);
		expect(left).to_equal(Rect{10, 5, 3, 4});
		expect(right).to_equal(Rect{13, 5, 5, 4});
		expect(left.w + right.w).to_equal(r.w);
	});
	it("insets by a uniform amount on every side", _ {
		Rect r{2, 3, 10, 6};
		expect(r.inset(1)).to_equal(Rect{3, 4, 8, 4});
		expect(r.inset(0)).to_equal(Rect{2, 3, 10, 6});
		// Negative grows it -- the same "either direction" shape as gfx::oled::Rect::inset.
		expect(r.inset(-2)).to_equal(Rect{0, 1, 14, 10});
	});
});

DELUGE_SPEC(ui_rect_spec, grid_rect)
