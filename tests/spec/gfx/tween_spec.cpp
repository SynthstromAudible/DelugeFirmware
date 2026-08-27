#include "cppspec.hpp"
#include "gfx/grid/tween.h"

using namespace deluge::gfx::grid;

// clang-format off
describe tween("gfx::grid tweens", $ {
	it("fade hits both endpoints exactly", _ {
		PadGrid a(kDelugeGrid), b(kDelugeGrid), out(kDelugeGrid);
		a.set(2, 2, RGB{200, 0, 0});
		b.set(2, 2, RGB{0, 0, 200});
		fade(a, b, 0, out);
		expect(out.at(2, 2)).to_equal(RGB{200, 0, 0});
		fade(a, b, 65535, out);
		expect(out.at(2, 2)).to_equal(RGB{0, 0, 200});
		expect(out.dirty_rows()).to_equal(0xFFu);
	});

	it("scroll_horizontal moves main pads and pulls incoming columns in, leaving the sidebar alone", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out(kDelugeGrid);
		a.set(0, 0, RGB{9, 9, 9});
		a.set(16, 0, RGB{7, 7, 7});      // sidebar
		in.set(15, 0, RGB{1, 2, 3});     // rightmost incoming main column
		scroll_horizontal(a, in, 1, out); // content moves right by one
		expect(out.at(1, 0)).to_equal(RGB{9, 9, 9});
		expect(out.at(0, 0)).to_equal(RGB{1, 2, 3});
		expect(out.at(16, 0)).to_equal(RGB{7, 7, 7});
		in.set(0, 0, RGB{4, 4, 4});
		scroll_horizontal(a, in, -1, out); // content moves left by one; column 15 comes from incoming's column 0
		expect(out.at(15, 0)).to_equal(RGB{4, 4, 4});
		expect(out.at(0, 0)).to_equal(RGB{0, 0, 0});
	});

	it("scroll_vertical moves rows", _ {
		PadGrid a(kDelugeGrid), in(kDelugeGrid), out(kDelugeGrid);
		a.set(3, 0, RGB{5, 5, 5});
		in.set(3, 7, RGB{6, 6, 6});
		scroll_vertical(a, in, 1, out);
		expect(out.at(3, 1)).to_equal(RGB{5, 5, 5});
		expect(out.at(3, 0)).to_equal(RGB{6, 6, 6});
	});

	it("grey_out dims main pads only", _ {
		PadGrid a(kDelugeGrid), out(kDelugeGrid);
		a.set(0, 0, RGB{255, 255, 255});
		a.set(17, 0, RGB{255, 255, 255});
		// RGB::greyOut's proportion is on a ~0..6500000 scale (see pad_leds.cpp/session_view.cpp/
		// load_song_ui.cpp callers), not the 0..65535 scale RGB::blend's index uses -- 65536/2 leaves
		// a white pixel completely unchanged under the real formula, so use a value on the real scale.
		grey_out(a, 6500000 / 2, out);
		expect(out.at(0, 0).r).to_be_less_than(uint8_t(255));
		expect(out.at(17, 0)).to_equal(RGB{255, 255, 255});
	});
});

// Not CPPSPEC_SPEC(tween): create_test_sourcelist derives the extern function name from this file's
// path relative to tests/spec (slashes become underscores) — see canvas_golden_spec.cpp.
int gfx_tween_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(tween).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE; // needs <cstdlib>
}
