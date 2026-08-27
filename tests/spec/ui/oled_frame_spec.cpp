#include "support/frames.h"
#include "ui/oled/frame.h"

#include "cppspec.hpp"

using namespace deluge::ui::oled;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

// clang-format off
describe oled_frame("ui::oled::OledFrame", $ {
	it("translates by visible_top and clips to the visible area", _ {
		uitest::Rig48 r;
		r.frame.pixel(0, 0);                       // local (0,0) -> canvas (0,5)
		expect(r.lit(0, 5)).to_be_true();
		expect(r.lit(0, 0)).to_be_false();         // the bezel rows stay dark
		r.frame.pixel(0, 43);                      // just past the 43 visible rows: clipped
		expect(r.lit(0, 48 - 1)).to_be_false();    // canvas (0,48) does not exist; (0,47) is the last row
		r.frame.pixel(0, 42);                      // the last visible row DOES draw
		expect(r.lit(0, 47)).to_be_true();
		r.frame.pixel(0, -1);                      // above the frame: clipped, does not reach row 4
		expect(r.lit(0, 4)).to_be_false();
		expect(r.frame.bounds()).to_equal(Rect{0, 0, 128, 43});
	});
	it("regions translate and clip, and nest", _ {
		uitest::Rig64 r;
		r.frame.region(Rect{10, 10, 20, 20}, [&self](OledFrame& f) {
			expect(f.bounds()).to_equal(Rect{0, 0, 20, 20});
			f.pixel(0, 0);                          // -> (10,10)
			f.pixel(25, 0);                         // outside: clipped
			f.region(Rect{5, 5, 5, 5}, [&self](OledFrame& g) {
				expect(g.bounds()).to_equal(Rect{0, 0, 5, 5});
				g.pixel(0, 0);                       // -> (15,15)
				g.pixel(6, 0);                       // outside the INNER region, inside the outer: clipped
			});
		});
		expect(r.lit(10, 10)).to_be_true();
		expect(r.lit(35, 10)).to_be_false();
		expect(r.lit(15, 15)).to_be_true();
		expect(r.lit(21, 15)).to_be_false();        // would be lit if the nested clip were the outer one
	});
	it("clips a region that hangs off its parent, and still reports its full local rect", _ {
		uitest::Rig64 r;
		// Bottom-right corner: 20 wide but only 8 of those columns are on the panel.
		r.frame.region(Rect{120, 60, 20, 20}, [&self](OledFrame& f) {
			expect(f.bounds()).to_equal(Rect{0, 0, 20, 20});   // spec B section 4: the requested rect
			f.hline(0, 0, 19);                       // asks for 20 columns; 8 exist
			f.vline(0, 0, 19);
		});
		expect(r.lit(127, 60)).to_be_true();
		expect(r.lit(120, 63)).to_be_true();
	});
	it("keeps local (0,0) at the region's corner when it hangs off the left or top", _ {
		uitest::Rig64 r;
		// The case a clipped bounds() would get wrong: reporting {0,0,6,7} here would move the
		// widget's own origin four columns right and three rows down without telling it.
		r.frame.region(Rect{-4, -3, 10, 10}, [&self](OledFrame& f) {
			expect(f.bounds()).to_equal(Rect{0, 0, 10, 10});
			f.pixel(0, 0);                           // -> surface (-4,-3): off-panel, clipped
			f.pixel(5, 5);                           // -> surface (1,2): the origin has not moved
		});
		expect(r.lit(1, 2)).to_be_true();
		expect(r.lit(5, 5)).to_be_false();           // where a clipped-bounds origin would have put it
	});
	it("clips spans, outlines and filled areas to the region", _ {
		uitest::Rig64 r;
		r.frame.region(Rect{10, 10, 10, 10}, [](OledFrame& f) {
			f.hline(2, -50, 500);                    // clamped to the region's columns
			f.vline(2, -50, 500);
			f.rect(f.bounds());                      // outline exactly on the region edge
		});
		expect(r.lit(10, 12)).to_be_true();          // hline start
		expect(r.lit(19, 12)).to_be_true();          // hline end
		expect(r.lit(9, 12)).to_be_false();          // ...and no further
		expect(r.lit(20, 12)).to_be_false();
		expect(r.lit(12, 10)).to_be_true();          // vline start
		expect(r.lit(12, 19)).to_be_true();
		expect(r.lit(12, 9)).to_be_false();
		expect(r.lit(12, 20)).to_be_false();
		expect(r.lit(10, 10)).to_be_true();          // rect corners
		expect(r.lit(19, 19)).to_be_true();

		// A filled invert of the whole region flips exactly the region's pixels: the rect's
		// corner goes dark, an untouched interior pixel lights up, and neither neighbouring
		// column outside the region moves.
		r.frame.region(Rect{10, 10, 10, 10}, [](OledFrame& f) { f.invert(f.bounds()); });
		expect(r.lit(10, 10)).to_be_false();
		expect(r.lit(14, 14)).to_be_true();          // interior, dark before the invert
		expect(r.lit(9, 10)).to_be_false();
		expect(r.lit(20, 10)).to_be_false();
	});
	it("clears only within the region", _ {
		uitest::Rig64 r;
		r.frame.hline(5, 0, 127);
		r.frame.region(Rect{40, 0, 10, 10}, [](OledFrame& f) { f.clear(f.bounds()); });
		expect(r.lit(39, 5)).to_be_true();
		expect(r.lit(40, 5)).to_be_false();
		expect(r.lit(49, 5)).to_be_false();
		expect(r.lit(50, 5)).to_be_true();
	});
	it("skips a blit that would not fit the region rather than overdrawing", _ {
		uitest::Rig64 r;
		// One byte per column, all eight rows set.
		const uint8_t bar[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
		r.frame.region(Rect{0, 0, 4, 8}, [&bar](OledFrame& f) {
			f.graphic(bar, 0, 0, 8, 8, 1);           // 8 columns into a 4-column region: skipped whole
			f.graphic(bar, 0, 0, 4, 8, 1);           // fits: drawn
		});
		expect(r.lit(0, 0)).to_be_true();
		expect(r.lit(3, 7)).to_be_true();
		expect(r.lit(4, 0)).to_be_false();           // the oversized blit left nothing behind
	});
});
// clang-format on

// Not CPPSPEC_SPEC(oled_frame): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `ui_oled_frame_spec` for
// ui/oled_frame_spec.cpp, which does not match the `oled_frame` describe-block name the macro
// would otherwise assume. Spelled out by hand so the exported symbol matches what the generated
// driver (all_specs.cpp) declares `extern` (see ui/led_frame_spec.cpp for the same pattern).
int ui_oled_frame_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(oled_frame).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
