#include "hid/display/oled_canvas/canvas.h"
#include "support/golden.h"
#include "support/pbm.h"

#include "../support/spec_main.h"
#include "cppspec.hpp"

using deluge::hid::display::Icon;
using deluge::hid::display::oled_canvas::Canvas;
using deluge::hid::display::oled_canvas::DrawLineOptions;

namespace {
constexpr int W = 128, H = 48;

std::string pbm(Canvas& c) {
	return page_major_to_pbm(&c.hackGetImageStore()[0][0], W, H);
}

// 8x8 arrow used by the icon scenes: one byte per column, bit 0 = top.
constexpr uint8_t kArrowData[8] = {0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00, 0x00};
constexpr Icon kArrow{kArrowData, 8, 8, 1};
} // namespace

// clang-format off
describe canvas_golden("Canvas golden frames", $ {
	it("primitives", _ {
		Canvas c; c.clear();
		c.drawPixel(0, 5); c.drawPixel(127, 47);
		c.drawHorizontalLine(10, 4, 60);
		c.drawVerticalLine(70, 6, 40);
		c.drawLine(80, 8, 120, 40);
		c.drawRectangle(2, 12, 30, 30);
		c.drawCircle(100, 20, 7, false);
		c.drawCircle(110, 38, 5, true);
		expect_golden(self, "oled/primitives.pbm", pbm(c));
	});

	it("text_all_fonts", _ {
		Canvas c; c.clear();
		c.drawString("Ag5", 0, 6, 6, 5);     // 5px
		c.drawString("Ag7", 24, 6, 6, 7);    // apple 7
		c.drawString("Ag9", 48, 6, 6, 9);    // apple, y+1
		c.drawString("Ag10", 0, 16, 9, 10);  // metric 9
		c.drawString("Ag13", 40, 16, 9, 13); // metric 13
		c.drawString("Ag20", 0, 28, 18, 20); // metric 20
		expect_golden(self, "oled/text_all_fonts.pbm", pbm(c));
	});

	it("text_alignment", _ {
		Canvas c; c.clear();
		c.drawStringCentred("CENTRE", 6, 6, 7);
		c.drawStringAlignRight("RIGHT", 16, 6, 7);
		c.drawStringCentered("MID", 10, 26, 6, 9, 60);
		c.drawStringCentredShrinkIfNecessary("THIS TITLE IS FAR TOO LONG TO FIT", 36, 9, 10);
		c.drawString("SCROLL", 0, 40, 6, 7, 3, 40, true);
		expect_golden(self, "oled/text_alignment.pbm", pbm(c));
	});

	it("icons", _ {
		Canvas c; c.clear();
		c.drawIcon(kArrow, 4, 6);
		c.drawIcon(kArrow, 20, 9, true);
		c.drawIconCentered(kArrow, 40, 40, 20);
		c.drawGraphicMultiLine(kArrowData, 100, 13, 8, 8, 1);
		expect_golden(self, "oled/icons.pbm", pbm(c));
	});

	it("rounded_and_invert", _ {
		Canvas c; c.clear();
		Canvas::roundedCornersEnabled = true;
		c.drawRectangleRounded(2, 6, 40, 20);
		c.drawRectangleRounded(50, 6, 90, 20, deluge::hid::display::oled_canvas::BIG);
		c.drawString("INV", 4, 26, 6, 7);
		c.invertArea(2, 30, 24, 34);
		c.invertAreaRounded(40, 30, 24, 34);
		Canvas::menuHighlightingInverts = true;
		c.invertLeftEdgeForMenuHighlighting(80, 20, 24, 34);
		Canvas::menuHighlightingInverts = false;
		c.invertLeftEdgeForMenuHighlighting(104, 20, 24, 34);
		c.drawCircle(12, 12, 4, true);
		c.clearAreaExact(10, 10, 14, 14);
		Canvas::roundedCornersEnabled = false;
		c.drawRectangleRounded(2, 38, 40, 46);
		Canvas::roundedCornersEnabled = true;
		expect_golden(self, "oled/rounded_and_invert.pbm", pbm(c));
	});

	it("graphic_multiline", _ {
		Canvas c; c.clear();
		// 16 columns x 2 bytes tall, drawn at three y offsets to exercise page straddling.
		uint8_t g[32];
		for (int i = 0; i < 32; i++) g[i] = (uint8_t)(0x81 + i);
		c.drawGraphicMultiLine(g, 0, 5, 16, 16, 2);
		c.drawGraphicMultiLine(g, 20, 9, 16, 16, 2);
		c.drawGraphicMultiLine(g, 40, 15, 16, 12, 2);
		c.drawGraphicMultiLine(g, 120, 20, 16, 16, 2); // clipped at right edge
		expect_golden(self, "oled/graphic_multiline.pbm", pbm(c));
	});

	it("line_options", _ {
		Canvas c; c.clear();
		c.drawLine(0, 6, 127, 47, DrawLineOptions{.thick = true});
		c.drawLine(0, 47, 127, 6, DrawLineOptions{.min_x = 20, .max_x = 100});
		c.drawScreenTitle("TITLE");
		expect_golden(self, "oled/line_options.pbm", pbm(c));
	});
});

DELUGE_SPEC(gfx_canvas_golden_spec, canvas_golden)
