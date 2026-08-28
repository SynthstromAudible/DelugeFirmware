#include "gfx/oled/canvas.h"
#include "hid/display/oled_canvas/canvas.h"
#include "support/golden.h"
#include "support/pbm.h"

#include "../support/spec_main.h"
#include "cppspec.hpp"

#include <cstring>

using deluge::hid::display::Icon;
using deluge::hid::display::oled_canvas::Canvas;
using deluge::hid::display::oled_canvas::DrawLineOptions;

namespace g = deluge::gfx::oled;

namespace {
constexpr int W = 128, H = 48;

std::string pbm(Canvas& c) {
	return page_major_to_pbm(&c.hackGetImageStore()[0][0], W, H);
}

// 8x8 arrow used by the icon scenes: one byte per column, bit 0 = top.
constexpr uint8_t kArrowData[8] = {0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00, 0x00};
constexpr Icon kArrow{kArrowData, 8, 8, 1};
constexpr g::Icon kArrowGfx{kArrowData, 8, 8, 1};

int lit_pixels(std::span<const uint8_t> bytes) {
	int n = 0;
	for (uint8_t b : bytes) {
		n += __builtin_popcount(b);
	}
	return n;
}

/// Zero every pixel outside \p r. Writes the bytes directly rather than going through
/// `clear_pixel` so the oracle cannot be affected by the very feature under test.
void mask_to(g::Canvas& c, g::Rect r) {
	const g::Geometry geom = c.geometry();
	for (int32_t y = 0; y < geom.height; ++y) {
		for (int32_t x = 0; x < geom.width; ++x) {
			if (!r.contains(g::Point{x, y})) {
				c.bytes()[(y >> 3) * geom.width + x] &= static_cast<uint8_t>(~(1u << (y & 7)));
			}
		}
	}
}

/// The exact oracle for a clip rect, needing no golden: run \p body on a canvas clipped to \p r,
/// run the very same calls on an unclipped scratch canvas, erase everything outside \p r there,
/// and require the two framebuffers to be byte-identical. Whatever the primitive does inside the
/// rectangle is by construction what it must keep doing; whatever it does outside must vanish.
///
/// Also insists the clipped result is not blank, so a primitive that clipped everything away
/// could not pass vacuously.
template <typename Self, typename Body>
void expect_clipped_to(Self& self, g::Rect r, Body&& body) {
	g::OwnedCanvas<g::kDelugeOled> clipped;
	g::OwnedCanvas<g::kDelugeOled> oracle;

	clipped.set_clip(r);
	body(static_cast<g::Canvas&>(clipped));
	clipped.clear_clip();

	body(static_cast<g::Canvas&>(oracle));
	mask_to(oracle, r);

	expect(lit_pixels(clipped.bytes()) > 0).to_be_true();
	expect(std::memcmp(clipped.bytes().data(), oracle.bytes().data(), clipped.bytes().size()) == 0).to_be_true();
}

/// The clip rect every case below is checked against. It deliberately starts and ends part-way
/// down a page (rows 12..30 span pages 1, 2 and 3), because the blitting routines write whole
/// bytes and a page-aligned rectangle would never exercise the page masks.
constexpr g::Rect kBox{20, 12, 24, 19};
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

	it("clips a character half outside the rect on each edge", _ {
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_char('W', 14, 16, 9, 13); });  // off the left
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_char('W', 39, 16, 9, 13); });  // off the right
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_char('W', 26, 6, 9, 13); });   // off the top
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_char('W', 26, 24, 9, 13); });  // off the bottom
	});

	it("clips a string half outside the rect on each edge", _ {
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_string("Ag13", 8, 16, 9, 13); });
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_string("Ag13", 36, 16, 9, 13); });
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_string("Ag13", 22, 5, 9, 13); });
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_string("Ag13", 22, 26, 9, 13); });
	});

	it("clips an icon half outside the rect", _ {
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_icon(kArrowGfx, 16, 14); });
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_icon(kArrowGfx, 40, 14, true); });
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_icon(kArrowGfx, 24, 27); });
		// Centred across a span wider than the rect: the padding puts most of it outside.
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.draw_icon_centered(kArrowGfx, 0, 40, 14); });
	});

	it("clips a multi-page graphic crossing a page boundary", _ {
		// 16 columns x 2 bytes tall at y = 9: rows 9..24, i.e. straddling pages 1, 2 and 3, with
		// the clip cutting it part-way down two of them.
		expect_clipped_to(self, kBox, [](g::Canvas& c) {
			uint8_t graphic[32];
			for (int i = 0; i < 32; i++) {
				graphic[i] = static_cast<uint8_t>(0x81 + i);
			}
			c.draw_graphic_multi_line(graphic, 14, 9, 16, 16, 2);
		});
		expect_clipped_to(self, kBox, [](g::Canvas& c) {
			uint8_t graphic[32];
			for (int i = 0; i < 32; i++) {
				graphic[i] = static_cast<uint8_t>(0x81 + i);
			}
			c.draw_graphic_multi_line(graphic, 34, 22, 16, 16, 2);
		});
	});

	it("clips a rounded invert half outside the rect", _ {
		expect_clipped_to(self, kBox, [](g::Canvas& c) { c.invert_area_rounded(10, 24, 8, 20); });
		expect_clipped_to(self, kBox, [](g::Canvas& c) {
			c.invert_area_rounded(30, 24, 22, 40, g::BorderRadius::Big);
		});
		// An invert over content, not just over blank space: the XOR must still land only inside.
		expect_clipped_to(self, kBox, [](g::Canvas& c) {
			c.draw_string("Ag13", 10, 14, 9, 13);
			c.invert_area_rounded(10, 24, 12, 28);
		});
	});

	it("clips the span, outline and fill primitives too", _ {
		expect_clipped_to(self, kBox, [](g::Canvas& c) {
			c.draw_horizontal_line(16, 0, 127);
			c.draw_vertical_line(30, 0, 47);
			c.draw_line(0, 0, 127, 47);
			c.draw_rectangle(10, 8, 50, 40);
			c.draw_rectangle_rounded(12, 10, 48, 38, g::BorderRadius::Big);
			c.draw_circle(24, 20, 12);
			c.draw_pixel(0, 0);
			c.draw_pixel(22, 14);
			c.invert_area(0, 128, 0, 47);
		});
	});

	it("clears only inside the clip", _ {
		g::OwnedCanvas<g::kDelugeOled> c;
		c.draw_horizontal_line(16, 0, 127);
		c.set_clip(kBox);
		c.clear();                                        // "the whole image", bounded by the clip
		c.clear_clip();
		expect(c.bytes()[(16 >> 3) * 128 + 19]).to_equal(uint8_t(1 << 0));   // just left of the clip
		expect(c.bytes()[(16 >> 3) * 128 + 20]).to_equal(uint8_t(0));        // inside it
		expect(c.bytes()[(16 >> 3) * 128 + 43]).to_equal(uint8_t(0));
		expect(c.bytes()[(16 >> 3) * 128 + 44]).to_equal(uint8_t(1 << 0));   // just right of it
	});

	it("draws nothing at all through an empty or off-canvas clip", _ {
		for (g::Rect nothing : {g::Rect{20, 12, 0, 19}, g::Rect{20, 12, 24, 0}, g::Rect{200, 12, 24, 19},
		                        g::Rect{-40, 12, 20, 19}}) {
			g::OwnedCanvas<g::kDelugeOled> c;
			c.set_clip(nothing);
			c.draw_string("Ag13", 22, 14, 9, 13);
			c.draw_icon(kArrowGfx, 22, 14);
			c.invert_area_rounded(0, 128, 0, 47);
			c.draw_rectangle(0, 0, 127, 47);
			expect(lit_pixels(c.bytes())).to_equal(0);
		}
	});

	it("hands the clip back and drops it again", _ {
		g::OwnedCanvas<g::kDelugeOled> c;
		expect(c.clip().has_value()).to_be_false();
		c.set_clip(g::Rect{1, 2, 3, 4});
		expect(c.clip().has_value()).to_be_true();
		expect(*c.clip() == g::Rect{1, 2, 3, 4}).to_be_true();   // as passed, not as clamped
		c.clear_clip();
		expect(c.clip().has_value()).to_be_false();
		c.draw_pixel(0, 0);
		expect(lit_pixels(c.bytes())).to_equal(1);
	});
});

DELUGE_SPEC(gfx_canvas_golden_spec, canvas_golden)
