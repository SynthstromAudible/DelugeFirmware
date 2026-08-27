#include "gfx/oled/canvas.h"
#include "hid/display/oled_canvas/canvas.h" // the old class, for parity
#include "support/pbm.h"

#include "cppspec.hpp"

#include <cstdlib>
#include <cstring>

namespace g = deluge::gfx::oled;
using OldCanvas = deluge::hid::display::oled_canvas::Canvas;

// clang-format off
describe canvas("gfx::oled::Canvas", $ {
	it("draws into caller storage of the right size", _ {
		uint8_t store[768] = {};
		g::Canvas c(store, g::kDelugeOled);
		c.draw_pixel(3, 9);
		expect(store[1 * 128 + 3]).to_equal(uint8_t(1 << 1));
		expect(c.bytes().size()).to_equal(size_t(768));
	});

	it("respects a 128x64 geometry with no hidden rows", _ {
		g::OwnedCanvas<g::Geometry{128, 64, 0}> c;
		c.clear();
		c.draw_horizontal_line(63, 0, 127);        // last row exists on 64-tall
		expect(c.bytes()[7 * 128 + 5]).to_equal(uint8_t(0x80));
		c.draw_string("EDGE", 100, 50, 6, 7);      // clipped, no write past the buffer
		expect(c.bytes().size()).to_equal(size_t(1024));
	});

	it("resolves kEdge and kCentre against the geometry", _ {
		g::OwnedCanvas<g::Geometry{64, 16, 0}> narrow;
		narrow.clear();
		narrow.draw_string_align_right("R", 4, 6, 7); // right_pos defaults to kEdge == 64
		bool anyInLastCols = false;
		for (int x = 56; x < 64; x++) anyInLastCols |= (narrow.bytes()[x] | narrow.bytes()[64 + x]) != 0;
		expect(anyInLastCols).to_be_true();
	});

	it("is byte-identical to the legacy canvas on the Deluge geometry", _ {
		OldCanvas old; old.clear();
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		old.drawRectangle(2, 12, 30, 30);            neu.draw_rectangle(2, 12, 30, 30);
		old.drawCircle(100, 20, 7, false);           neu.draw_circle(100, 20, 7, false);
		old.drawString("Ag13", 40, 16, 9, 13);       neu.draw_string("Ag13", 40, 16, 9, 13);
		old.drawStringCentred("CENTRE", 6, 6, 7);    neu.draw_string_centred("CENTRE", 6, 6, 7);
		old.drawLine(0, 6, 127, 47, {.thick = true}); neu.draw_line(0, 6, 127, 47, {.thick = true});
		old.drawRectangleRounded(50, 6, 90, 20, deluge::hid::display::oled_canvas::BIG);
		neu.draw_rectangle_rounded(50, 6, 90, 20, g::BIG, true);
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), 768)).to_equal(0);
		// Non-vacuous: two blank buffers would also compare equal.
		int lit = 0; for (uint8_t b : neu.bytes()) lit += __builtin_popcount(b);
		expect(lit > 500).to_be_true();
	});
});

// Not CPPSPEC_SPEC(canvas): create_test_sourcelist derives the extern function name from this file's
// path relative to tests/spec (slashes become underscores), i.e. `gfx_canvas_spec` for
// gfx/canvas_spec.cpp, which does not match the `canvas` describe-block name the macro would
// otherwise assume. Spelled out by hand so the exported symbol matches what the generated driver
// (all_specs.cpp) declares `extern` (see gfx/canvas_golden_spec.cpp for the same pattern).
int gfx_canvas_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(canvas).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
