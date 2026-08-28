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
	// split_rows(n, i) / split_cols(n, i): task-6 brief ruling 3's worked example, hand-computed --
	// 8 rows split 3 ways is base=8/3=2 rows each with remainder 8%3=2, so slices 0 and 1 (i < rem)
	// get one extra row (3, 3) and slice 2 gets the plain base (2): 3, 3, 2, summing back to 8.
	it("splits rows into n bands, front slices absorbing the remainder", _ {
		Rect r{5, 10, 4, 8}; // x=5, y=10, w=4, h=8
		expect(r.split_rows(3, 0)).to_equal(Rect{5, 10, 4, 3});
		expect(r.split_rows(3, 1)).to_equal(Rect{5, 13, 4, 3});
		expect(r.split_rows(3, 2)).to_equal(Rect{5, 16, 4, 2});
		// Evenly divisible: no remainder, every slice is the same size.
		Rect even{0, 0, 1, 9};
		expect(even.split_rows(3, 0)).to_equal(Rect{0, 0, 1, 3});
		expect(even.split_rows(3, 1)).to_equal(Rect{0, 3, 1, 3});
		expect(even.split_rows(3, 2)).to_equal(Rect{0, 6, 1, 3});
	});
	it("splits columns into n bands, front slices absorbing the remainder", _ {
		// Same rule, transposed: 8 cols split 3 ways is 3, 3, 2 in x instead of y.
		Rect r{10, 5, 8, 4};
		expect(r.split_cols(3, 0)).to_equal(Rect{10, 5, 3, 4});
		expect(r.split_cols(3, 1)).to_equal(Rect{13, 5, 3, 4});
		expect(r.split_cols(3, 2)).to_equal(Rect{16, 5, 2, 4});
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
