#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "gfx/oled/geometry.h"

using namespace deluge::gfx::oled;

// clang-format off
describe geometry("gfx::oled geometry", $ {
	it("derives pages and bytes from the panel size", _ {
		expect(kDelugeOled.pages()).to_equal(6u);
		expect(kDelugeOled.bytes()).to_equal(768u);
		expect(Geometry{128, 64, 0}.bytes()).to_equal(1024u);
		expect(kDelugeOled.visible_height()).to_equal(43);
	});

	it("splits rects without gaps or overlap", _ {
		Rect r{0, 5, 128, 43};
		auto [top, bottom] = r.split_rows(10);
		expect(top).to_equal(Rect{0, 5, 128, 10});
		expect(bottom).to_equal(Rect{0, 15, 128, 33});
		auto [left, right] = r.split_cols(32);
		expect(left.w + right.w).to_equal(128);
		expect(right.x).to_equal(32);
	});

	it("insets, contains and intersects", _ {
		Rect r{10, 10, 20, 20};
		expect(r.inset({1, 2, 3, 4})).to_equal(Rect{14, 11, 14, 16});
		expect(r.contains({10, 10})).to_be_true();
		expect(r.contains({30, 10})).to_be_false();
		expect(r.intersect(Rect{25, 25, 20, 20})).to_equal(Rect{25, 25, 5, 5});
		expect(r.intersect(Rect{40, 40, 5, 5}).w).to_equal(0);
		expect(r.centre()).to_equal(Point{20, 20});
	});
});

DELUGE_SPEC(gfx_geometry_spec, geometry)
