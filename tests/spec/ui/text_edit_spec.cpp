#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/oled/text_edit.h"

#include "../support/spec_main.h"
#include "cppspec.hpp"

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::BorderRadius;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

namespace {

// As tests/spec/ui/oled_frame_spec.cpp's own helper: a plain popcount over the framebuffer bytes,
// since this widget's own "did it draw anything" specs need a pixel count and no such counter is
// part of the toolkit itself.
int lit_pixels(std::span<const uint8_t> bytes) {
	int n = 0;
	for (uint8_t b : bytes) {
		n += __builtin_popcount(b);
	}
	return n;
}

constexpr const char* kLongString = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; // 36 chars

} // namespace

// clang-format off
describe text_edit_w("ui::oled::text_edit", $ {
	it("keeps the cursor visible by scrolling, within the rect's own character window", _ {
		// rect.w == 128, kTextSpacingX == 6 -> max_num_chars == 21; a cursor at 30 in a 36-char
		// string is well past that window from scroll == 0, so the widget must pull scroll in.
		uitest::Rig48 r;
		TextEditState st{.cursor = 30, .scroll = 0};
		text_edit(r.frame, r.frame.bounds(), kLongString, st);
		expect(st.scroll > 0).to_be_true();
		expect(st.cursor - st.scroll >= 0).to_be_true();
		expect(st.cursor - st.scroll < 21).to_be_true(); // inside the visible window
	});

	it("draws nothing but the cursor for an empty string", _ {
		uitest::Rig48 r;
		TextEditState st{};
		text_edit(r.frame, r.frame.bounds(), "", st);
		// A 1-character-wide (kTextSpacingX == 6), 3-row underline is the only thing that can be
		// lit: 18 pixels, all set by the invert flipping a blank band.
		expect(lit_pixels(r.canvas.bytes())).to_equal(18);
	});

	it("draws no cursor at all when cursor_lit is false", _ {
		uitest::Rig48 r;
		TextEditState st{.cursor_lit = false};
		text_edit(r.frame, r.frame.bounds(), "", st);
		expect(lit_pixels(r.canvas.bytes())).to_equal(0);
	});

	it("never scrolls left of the string's start", _ {
		uitest::Rig48 r;
		TextEditState st{.cursor = 0, .scroll = 5}; // an impossible starting scroll
		text_edit(r.frame, r.frame.bounds(), "HELLO", st);
		expect(st.scroll).to_equal(int16_t(0));
	});

	it("clamps a stale cursor into the current string's range on the same frame, per list's own "
	   "two-pass rule", _ {
		uitest::Rig48 r;
		TextEditState st{.cursor = 30, .scroll = 12}; // stale: yesterday's 36-char string
		text_edit(r.frame, r.frame.bounds(), "HI", st); // today's string is 2 chars long
		expect(st.cursor).to_equal(int16_t(2));
		expect(st.scroll).to_equal(int16_t(0));
	});

	it("matches qwerty_ui.cpp's own arithmetic for a short string with the cursor at the end", _ {
		// Legacy reference, replayed at the same canvas coordinates the port computes:
		// drawString("HELLO", 0, 0, 6, 9, 0, 21*6) then the blinking-underline invert at
		// cursorStartX = 0 + (5 - 0) * 6 = 30, y = [kTextSpacingY - 4, kTextSpacingY - 2] = [5, 7].
		uitest::Rig48 r;
		TextEditState st{.cursor = 5, .scroll = 0}; // "HELLO".size() == 5, cursor at the very end
		text_edit(r.frame, r.frame.bounds(), "HELLO", st);
		expect(st.scroll).to_equal(int16_t(0));

		r.reference.draw_string("HELLO", 0, r.frame.origin.y + 0, 6, 9, 0, 21 * 6);
		r.reference.invert_area(30, 6, r.frame.origin.y + 5, r.frame.origin.y + 7);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_edit_short_48.pbm", r.pbm());
	});

	it("matches the same short-string case on a 64-row, flush panel", _ {
		uitest::Rig64 r;
		TextEditState st{.cursor = 5, .scroll = 0};
		text_edit(r.frame, r.frame.bounds(), "HELLO", st);

		r.reference.draw_string("HELLO", 0, r.frame.origin.y + 0, 6, 9, 0, 21 * 6);
		r.reference.invert_area(30, 6, r.frame.origin.y + 5, r.frame.origin.y + 7);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_edit_short_64.pbm", r.pbm());
	});

	it("matches qwerty_ui.cpp's mid-string cursor presentation: an inverted, rounded tail from "
	   "the cursor to the rect's right edge, following style.invert_highlight (qwerty_ui.cpp:"
	   "156-170, list.cpp's D1 primitive)", _ {
		// cursor == 30, length == 36, max_num_chars == 21: minXScroll = min(30+3-21, 36-21+0) =
		// min(12, 15) = 12; maxXScroll = min(36-21+0, 30-3) = min(15, 27) = 15. scroll settles at
		// 12 either way -- the scroll clamp is unaffected by which cursor presentation is drawn.
		// atVeryEnd is false (30 != 36): this is legacy's *only* mid-string cursor case, not a
		// predictive-text overlay. scrollAmount = cursor - scroll = 18, so
		// highlightStartX = 0 + 6*18 - 1 = 107, one pixel into the gap after the visible '3'
		// (canvas columns 102-106) -- not on top of its ink. kDelugeStyle48/64 both set
		// invert_highlight = true, so this inverts a rounded box from column 107 to the rect's
		// right edge (127), 9 rows tall (the whole text cell, not just the 3-row underline band).
		uitest::Rig48 r;
		TextEditState st{.cursor = 30, .scroll = 0};
		text_edit(r.frame, r.frame.bounds(), kLongString, st);
		expect(st.scroll).to_equal(int16_t(12));

		r.reference.draw_string(std::string_view{kLongString}.substr(12), 0, r.frame.origin.y + 0, 6, 9, 0, 21 * 6);
		r.reference.invert_area_rounded(107, 21, r.frame.origin.y + 0, r.frame.origin.y + 8, BorderRadius::Small,
		                                 true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_edit_scrolled_48.pbm", r.pbm());
	});

	it("matches the same mid-string case on a 64-row, flush panel", _ {
		uitest::Rig64 r;
		TextEditState st{.cursor = 30, .scroll = 0};
		text_edit(r.frame, r.frame.bounds(), kLongString, st);
		expect(st.scroll).to_equal(int16_t(12));

		r.reference.draw_string(std::string_view{kLongString}.substr(12), 0, r.frame.origin.y + 0, 6, 9, 0, 21 * 6);
		r.reference.invert_area_rounded(107, 21, r.frame.origin.y + 0, r.frame.origin.y + 8, BorderRadius::Small,
		                                 true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_edit_scrolled_64.pbm", r.pbm());
	});

	it("draws the left-edge rule instead of an inverted tail when invert_highlight is off, "
	   "starting in the gap after the previous character", _ {
		// qwerty_ui.cpp:166: "Start the highlight in the 1px gap that follows the previous
		// character... otherwise the inversion eats a pixel column out of the letter before the
		// cursor." rect = {20,10,60,20} -> max_num_chars == 10; s == "ABC" (length 3), cursor == 2
		// (mid-string: "before C"), scroll settles at 0 -- scrollAmount == cursor - scroll == 2,
		// nonzero, so this exercises the general formula, not the scrollAmount == 0 special case
		// below. highlightStartX = rect.x + 6*2 - 1 == 31 (rect.x == 20): one column into the gap
		// after 'B', not on top of its ink -- verified against an independent replay of the same
		// primitives (draw_string then draw_vertical_line) rather than against assumptions about
		// which columns the 'B'/'C' glyphs themselves light up.
		uitest::Rig48 r;
		Style plain = kDelugeStyle48;
		plain.invert_highlight = false;
		OledFrame f = root_frame(r.canvas, plain, r.ctx);
		TextEditState st{.cursor = 2, .scroll = 0};
		text_edit(f, Rect{20, 10, 60, 20}, "ABC", st);
		expect(st.scroll).to_equal(int16_t(0));

		r.reference.draw_string("ABC", 20, f.origin.y + 10, 6, 9, 0, 20 + 10 * 6);
		r.reference.draw_vertical_line(31, f.origin.y + 10, f.origin.y + 18);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("starts the rule at the rect's own left edge when invert_highlight is off and the cursor "
	   "sits at the window's own start (qwerty_ui.cpp's NO_INVERSION && !scrollAmount special "
	   "case)", _ {
		// Without this special case the formula below would place the rule at rect.x - 1 == 19,
		// one column left of the box -- legacy's own comment explains why that is deliberately
		// avoided here: "highlightStartX = 0" (the panel's own left edge), generalised to the
		// rect's own left edge the same way every other panel-edge constant in this widget is.
		uitest::Rig48 r;
		Style plain = kDelugeStyle48;
		plain.invert_highlight = false;
		OledFrame f = root_frame(r.canvas, plain, r.ctx);
		TextEditState st{.cursor = 0, .scroll = 0}; // scrollAmount == cursor - scroll == 0
		text_edit(f, Rect{20, 10, 60, 20}, "ABC", st);
		expect(st.scroll).to_equal(int16_t(0));

		r.reference.draw_string("ABC", 20, f.origin.y + 10, 6, 9, 0, 20 + 10 * 6);
		r.reference.draw_vertical_line(20, f.origin.y + 10, f.origin.y + 18); // rect.x, not rect.x - 1
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("keeps state.scroll sticky across frames instead of recentring it every call", _ {
		// legacy's scrollPosHorizontal is a static the routine only ever nudges, never resets --
		// so a scroll further right than strictly required survives an unrelated frame that does
		// not need to move it.
		uitest::Rig48 r;
		TextEditState st{.cursor = 30, .scroll = 0};
		text_edit(r.frame, r.frame.bounds(), kLongString, st);
		expect(st.scroll).to_equal(int16_t(12));
		TextEditState st_again{.cursor = 30, .scroll = st.scroll};
		text_edit(r.frame, r.frame.bounds(), kLongString, st_again);
		expect(st_again.scroll).to_equal(st.scroll); // unchanged: already satisfies the window
	});

	it("lays the cursor out inside an offset, narrower rect", _ {
		// Same widget, arbitrary rect: max_num_chars == rect.w / kTextSpacingX == 60/6 == 10, and
		// everything -- text origin, end_x clip, cursor column -- is relative to rect, not to the
		// frame's own origin. An empty string keeps this free of any glyph ink to disambiguate
		// from the cursor band, as the whole-frame empty-string case above does. Cursor == length
		// == 0, so this is the at-the-end (blinking-bar) presentation, not the mid-string one.
		uitest::Rig48 r;
		TextEditState st{}; // cursor 0, scroll 0
		text_edit(r.frame, Rect{20, 10, 60, 20}, "", st);
		// Underline at rect.x + 0*6 = 20, band y = rect.y + 9 - 4 .. rect.y + 9 - 2 = 15..17
		// (frame-local), offset by the frame's own origin.y (5 on the 48-row panel).
		const int32_t band_y0 = r.frame.origin.y + 15;
		for (int x = 20; x < 26; x++) {
			expect(r.lit(x, band_y0)).to_be_true();
			expect(r.lit(x, band_y0 + 2)).to_be_true();
		}
		expect(r.lit(19, band_y0)).to_be_false();     // nothing left of rect.x
		expect(r.lit(26, band_y0)).to_be_false();     // nothing right of the 6px cursor column
		expect(r.lit(20, band_y0 - 1)).to_be_false(); // nothing above the band
		expect(r.lit(20, band_y0 + 3)).to_be_false(); // nothing below the band
	});
});
DELUGE_SPEC(ui_text_edit_spec, text_edit_w)
