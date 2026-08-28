#include "gfx/oled/canvas.h"
#include "hid/display/oled_canvas/canvas.h" // the old class, for parity
#include "support/pbm.h"

#include "cppspec.hpp"

#include <cstdlib>
#include <cstring>
#include <span>

namespace g = deluge::gfx::oled;
using OldCanvas = deluge::hid::display::oled_canvas::Canvas;
using OldOptions = deluge::hid::display::oled_canvas::DrawLineOptions;

namespace {
/// The Deluge panel, i.e. what both classes render at kDelugeOled.
constexpr int kBytes = 768;

/// Lit pixels, so a parity check can prove it is not comparing two blank buffers.
int lit_pixels(std::span<const uint8_t> bytes) {
	int n = 0;
	for (uint8_t b : bytes) {
		n += __builtin_popcount(b);
	}
	return n;
}

// 8x8 arrow, as used by the icon golden scene: one byte per column, bit 0 = top.
constexpr uint8_t kArrowData[8] = {0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00, 0x00};
constexpr g::Icon kArrow{kArrowData, 8, 8, 1};
} // namespace

// clang-format off
describe canvas("gfx::oled::Canvas", $ {
	it("draws into caller storage of the right size", _ {
		uint8_t store[768] = {};
		g::Canvas c(store, g::kDelugeOled);
		c.draw_pixel(3, 9);
		expect(store[1 * 128 + 3]).to_equal(uint8_t(1 << 1));
		expect(c.bytes().size()).to_equal(size_t(768));
	});

	it("accepts storage sized exactly to the geometry", _ {
		// The constructor's precondition is `storage.size() >= g.bytes()`; kDelugeOled needs
		// (48/8) * 128 == 768. Exactly 768 is the boundary case and must be accepted, so the
		// assert cannot have been written as a strict `>`.
		// The undersized case is deliberately NOT exercised: it trips an assert(), and CppSpec
		// has no death-test facility to catch the resulting abort.
		expect(g::kDelugeOled.bytes()).to_equal(uint32_t(768));
		uint8_t exact[768] = {};
		g::Canvas c(std::span<uint8_t>(exact, 768), g::kDelugeOled);
		expect(c.bytes().size()).to_equal(size_t(768));
		c.draw_pixel(127, 47); // the last pixel of the last page still lands inside the span
		expect(exact[5 * 128 + 127]).to_equal(uint8_t(1 << 7));
	});

	it("respects a 128x64 geometry and never writes past the caller's span", _ {
		// The canvas gets the middle 1024 bytes; the 32 bytes on either side are guard
		// bytes that nothing on-canvas may touch.
		uint8_t store[32 + 1024 + 32] = {};
		g::Canvas c(std::span<uint8_t>(&store[32], 1024), g::Geometry{128, 64, 0});
		c.clear();
		c.draw_horizontal_line(63, 0, 127);        // last row exists on 64-tall
		expect(c.bytes()[7 * 128 + 5]).to_equal(uint8_t(0x80));
		c.draw_string("EDGE", 100, 50, 6, 7);      // clipped, no write past the buffer
		c.draw_string("Y", 0, 64, 6, 7);           // first row past the bottom page
		c.draw_string("Y", 0, -8, 6, 7);           // one page above the top
		c.draw_icon(kArrow, -4, 0);                // off the left edge
		c.draw_icon(kArrow, 126, 24);              // straddling the right edge
		c.draw_graphic_multi_line(kArrowData, 128, 8, 8, 8, 1); // exactly off the right edge
		expect(c.bytes().size()).to_equal(size_t(1024));
		bool guardsIntact = true;
		for (int i = 0; i < 32; i++) guardsIntact &= (store[i] == 0) && (store[32 + 1024 + i] == 0);
		expect(guardsIntact).to_be_true();
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
		OldCanvas::roundedCornersEnabled = true;
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		old.drawRectangle(2, 12, 30, 30);            neu.draw_rectangle(2, 12, 30, 30);
		old.drawCircle(100, 20, 7, false);           neu.draw_circle(100, 20, 7, false);
		old.drawString("Ag13", 40, 16, 9, 13);       neu.draw_string("Ag13", 40, 16, 9, 13);
		old.drawStringCentred("CENTRE", 6, 6, 7);    neu.draw_string_centred("CENTRE", 6, 6, 7);
		old.drawLine(0, 6, 127, 47, {.thick = true}); neu.draw_line(0, 6, 127, 47, {.thick = true});
		old.drawRectangleRounded(50, 6, 90, 20, deluge::hid::display::oled_canvas::BIG);
		neu.draw_rectangle_rounded(50, 6, 90, 20, g::BorderRadius::Big, true);
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), kBytes)).to_equal(0);
		// Non-vacuous: two blank buffers would also compare equal.
		expect(lit_pixels(neu.bytes()) > 500).to_be_true();
	});

	// The seven scenes below replay tests/spec/gfx/canvas_golden_spec.cpp call for call, so the
	// parity net covers every ported method rather than a sample of them. Static policy flags on
	// the legacy class are set explicitly in each scene rather than inherited from whatever ran
	// before, and the gfx side passes the equivalent `rounded` argument.

	it("matches the legacy canvas on the primitives scene", _ {
		OldCanvas old; old.clear();
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		old.drawPixel(0, 5);                        neu.draw_pixel(0, 5);
		old.drawPixel(127, 47);                     neu.draw_pixel(127, 47);
		old.drawHorizontalLine(10, 4, 60);          neu.draw_horizontal_line(10, 4, 60);
		old.drawVerticalLine(70, 6, 40);            neu.draw_vertical_line(70, 6, 40);
		old.drawLine(80, 8, 120, 40);               neu.draw_line(80, 8, 120, 40);
		old.drawRectangle(2, 12, 30, 30);           neu.draw_rectangle(2, 12, 30, 30);
		old.drawCircle(100, 20, 7, false);          neu.draw_circle(100, 20, 7, false);
		old.drawCircle(110, 38, 5, true);           neu.draw_circle(110, 38, 5, true);
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), kBytes)).to_equal(0);
		expect(lit_pixels(neu.bytes()) > 0).to_be_true();
	});

	it("matches the legacy canvas on the text_all_fonts scene", _ {
		OldCanvas old; old.clear();
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		old.drawString("Ag5", 0, 6, 6, 5);          neu.draw_string("Ag5", 0, 6, 6, 5);
		old.drawString("Ag7", 24, 6, 6, 7);         neu.draw_string("Ag7", 24, 6, 6, 7);
		old.drawString("Ag9", 48, 6, 6, 9);         neu.draw_string("Ag9", 48, 6, 6, 9);
		old.drawString("Ag10", 0, 16, 9, 10);       neu.draw_string("Ag10", 0, 16, 9, 10);
		old.drawString("Ag13", 40, 16, 9, 13);      neu.draw_string("Ag13", 40, 16, 9, 13);
		old.drawString("Ag20", 0, 28, 18, 20);      neu.draw_string("Ag20", 0, 28, 18, 20);
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), kBytes)).to_equal(0);
		expect(lit_pixels(neu.bytes()) > 0).to_be_true();
	});

	it("matches the legacy canvas on the text_alignment scene", _ {
		OldCanvas old; old.clear();
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		old.drawStringCentred("CENTRE", 6, 6, 7);   neu.draw_string_centred("CENTRE", 6, 6, 7);
		old.drawStringAlignRight("RIGHT", 16, 6, 7); neu.draw_string_align_right("RIGHT", 16, 6, 7);
		// drawStringCentered is the one method rewritten by hand (the etl::string<24> trim loop
		// became a stack buffer), so it matters most that this line agrees byte for byte.
		old.drawStringCentered("MID", 10, 26, 6, 9, 60);
		neu.draw_string_centered("MID", 10, 26, 6, 9, 60);
		old.drawStringCentredShrinkIfNecessary("THIS TITLE IS FAR TOO LONG TO FIT", 36, 9, 10);
		neu.draw_string_centred_shrink_if_necessary("THIS TITLE IS FAR TOO LONG TO FIT", 36, 9, 10);
		old.drawString("SCROLL", 0, 40, 6, 7, 3, 40, true);
		neu.draw_string("SCROLL", 0, 40, 6, 7, 3, 40, true);
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), kBytes)).to_equal(0);
		expect(lit_pixels(neu.bytes()) > 0).to_be_true();
	});

	it("matches the legacy canvas on the icons scene", _ {
		OldCanvas old; old.clear();
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		old.drawIcon(kArrow, 4, 6);                 neu.draw_icon(kArrow, 4, 6);
		old.drawIcon(kArrow, 20, 9, true);          neu.draw_icon(kArrow, 20, 9, true);
		old.drawIconCentered(kArrow, 40, 40, 20);   neu.draw_icon_centered(kArrow, 40, 40, 20);
		old.drawGraphicMultiLine(kArrowData, 100, 13, 8, 8, 1);
		neu.draw_graphic_multi_line(kArrowData, 100, 13, 8, 8, 1);
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), kBytes)).to_equal(0);
		expect(lit_pixels(neu.bytes()) > 0).to_be_true();
	});

	it("matches the legacy canvas on the rounded_and_invert scene", _ {
		OldCanvas old; old.clear();
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		OldCanvas::roundedCornersEnabled = true;
		old.drawRectangleRounded(2, 6, 40, 20);
		neu.draw_rectangle_rounded(2, 6, 40, 20, g::BorderRadius::Small, true);
		OldCanvas::roundedCornersEnabled = true;
		old.drawRectangleRounded(50, 6, 90, 20, deluge::hid::display::oled_canvas::BIG);
		neu.draw_rectangle_rounded(50, 6, 90, 20, g::BorderRadius::Big, true);
		old.drawString("INV", 4, 26, 6, 7);         neu.draw_string("INV", 4, 26, 6, 7);
		old.invertArea(2, 30, 24, 34);              neu.invert_area(2, 30, 24, 34);
		OldCanvas::roundedCornersEnabled = true;
		old.invertAreaRounded(40, 30, 24, 34);
		neu.invert_area_rounded(40, 30, 24, 34, g::BorderRadius::Small, true);
		// invertLeftEdgeForMenuHighlighting is policy and has no gfx counterpart: its two
		// branches are replayed directly on the gfx side.
		OldCanvas::menuHighlightingInverts = true;
		OldCanvas::roundedCornersEnabled = true;
		old.invertLeftEdgeForMenuHighlighting(80, 20, 24, 34);
		neu.invert_area_rounded(80, 20, 24, 34, g::BorderRadius::Small, true);
		OldCanvas::menuHighlightingInverts = false;
		old.invertLeftEdgeForMenuHighlighting(104, 20, 24, 34);
		neu.draw_vertical_line(104, 24, 34);
		old.drawCircle(12, 12, 4, true);            neu.draw_circle(12, 12, 4, true);
		old.clearAreaExact(10, 10, 14, 14);         neu.clear_area_exact(10, 10, 14, 14);
		OldCanvas::roundedCornersEnabled = false;
		old.drawRectangleRounded(2, 38, 40, 46);
		neu.draw_rectangle_rounded(2, 38, 40, 46, g::BorderRadius::Small, false);
		OldCanvas::roundedCornersEnabled = true;
		OldCanvas::menuHighlightingInverts = true;
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), kBytes)).to_equal(0);
		expect(lit_pixels(neu.bytes()) > 0).to_be_true();
	});

	it("matches the legacy canvas on the graphic_multiline scene", _ {
		OldCanvas old; old.clear();
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		// 16 columns x 2 bytes tall, drawn at three y offsets to exercise page straddling.
		uint8_t graphic[32];
		for (int i = 0; i < 32; i++) graphic[i] = (uint8_t)(0x81 + i);
		old.drawGraphicMultiLine(graphic, 0, 5, 16, 16, 2);
		neu.draw_graphic_multi_line(graphic, 0, 5, 16, 16, 2);
		old.drawGraphicMultiLine(graphic, 20, 9, 16, 16, 2);
		neu.draw_graphic_multi_line(graphic, 20, 9, 16, 16, 2);
		old.drawGraphicMultiLine(graphic, 40, 15, 16, 12, 2);
		neu.draw_graphic_multi_line(graphic, 40, 15, 16, 12, 2);
		old.drawGraphicMultiLine(graphic, 120, 20, 16, 16, 2); // clipped at right edge
		neu.draw_graphic_multi_line(graphic, 120, 20, 16, 16, 2);
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), kBytes)).to_equal(0);
		expect(lit_pixels(neu.bytes()) > 0).to_be_true();
	});

	it("matches the legacy canvas on the line_options scene", _ {
		OldCanvas old; old.clear();
		g::OwnedCanvas<g::kDelugeOled> neu; neu.clear();
		old.drawLine(0, 6, 127, 47, OldOptions{.thick = true});
		neu.draw_line(0, 6, 127, 47, g::DrawLineOptions{.thick = true});
		old.drawLine(0, 47, 127, 6, OldOptions{.min_x = 20, .max_x = 100});
		neu.draw_line(0, 47, 127, 6, g::DrawLineOptions{.min_x = 20, .max_x = 100});
		old.drawScreenTitle("TITLE");               neu.draw_screen_title("TITLE");
		expect(std::memcmp(&old.hackGetImageStore()[0][0], neu.bytes().data(), kBytes)).to_equal(0);
		expect(lit_pixels(neu.bytes()) > 0).to_be_true();
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
