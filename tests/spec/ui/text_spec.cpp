#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/core/style.h"
#include "ui/oled/text.h"

#include "../support/spec_main.h"
#include "cppspec.hpp"

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::FontId;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

// Each scene below pins two things. First the individual pixel coordinates the legacy routine
// puts ink on -- read off the legacy renderer, not off this port, so a port that merely agrees
// with itself fails. Second, byte-for-byte equality with the legacy routine drawn into a second
// canvas of the same geometry: the assertions name the coordinates that matter, the comparison
// catches everything else.
//
// Every reference render below spells the legacy textWidth/textHeight as a *literal* (6 and 9
// for the Apple font, 11 and 13 for MetricBold13, ...), never as spacing_x_for/text_height_for.
// Feeding those two tables into both sides of the comparison is what let a wrong entry pass
// review round 0; the literals are what make these tables testable.

// clang-format off
describe text_widgets("ui::oled text & title", $ {
	it("maps each FontId to its legacy textHeight and textWidth", _ {
		// FontId::Apple7 is the legacy *height 9* shape (kTextSpacingY), not height 7:
		// font_for_text_height returns kApple7Up for 9, which carries y_adjust = 1 and so
		// draws the same glyphs one row lower than the height 7/8 entry
		// (gfx/oled/font.cpp:13, :26-30). Height 9 is 126 of the 155 legacy Apple call sites.
		expect(text_height_for(FontId::Apple7)).to_equal(9);
		expect(spacing_x_for(FontId::Apple7)).to_equal(6);
		// MetricBold13's advance is kTextBigSpacingX = 11 (definitions_cxx.hpp:979), NOT the
		// title font's 9. spacing_x_for feeds draw_string's text_width, which the shrink path
		// honours, so the wrong value flips the shrink decision itself.
		expect(spacing_x_for(FontId::MetricBold13)).to_equal(11);
		expect(text_height_for(FontId::MetricBold13)).to_equal(13);
		expect(spacing_x_for(FontId::MetricBold9)).to_equal(9);
		expect(text_height_for(FontId::MetricBold9)).to_equal(10);
		expect(spacing_x_for(FontId::MetricBold20)).to_equal(18);
		expect(text_height_for(FontId::MetricBold20)).to_equal(20);
		expect(spacing_x_for(FontId::Px5)).to_equal(4);      // kTextSmallSpacingX
		expect(text_height_for(FontId::Px5)).to_equal(5);
	});
	it("measures by the legacy advance metrics", _ {
		// The Apple face is monospaced at kTextSpacingX for every height in 7..9, so the C1 fix
		// moves the height but not the width.
		expect(measure("C", FontId::Apple7).w).to_equal(6);
		expect(measure("C", FontId::Apple7).h).to_equal(9);
		expect(measure("SOUND", FontId::MetricBold9).w).to_equal(45);
		// MetricBold9 is *selected* by height 10 (kTextTitleSizeY) though it draws 9 rows;
		// 10 is the number the legacy layout code spaces by, so it is what measure reports.
		expect(measure("SOUND", FontId::MetricBold9).h).to_equal(10);
		expect(measure("SOUND", FontId::MetricBold13).w).to_equal(67);
		expect(measure("SOUND", FontId::MetricBold20).w).to_equal(96);
	});
	it("draws a title at the legacy position with its separator", _ {
		uitest::Rig48 r;
		title(r.frame, "SOUND");
		// Canvas::draw_screen_title (canvas.cpp:688-699 at 1576af582): start_y = 1 + visible_top
		// (:689-690), separator_y = 12 + visible_top (:692), separator spanning 0..width-1
		// (:697). visible_top is 5 here, so rows 6 and 17.
		expect(r.lit(2, 6)).to_be_true();            // top row of the 'S'
		expect(r.lit(1, 8)).to_be_true();            // its left stem
		expect(r.lit(2, 5)).to_be_false();           // nothing on the bezel row above it
		expect(r.lit(0, 17)).to_be_true();           // separator, full width
		expect(r.lit(127, 17)).to_be_true();
		expect(r.lit(0, 16)).to_be_false();
		expect(r.lit(0, 18)).to_be_false();

		r.reference.draw_screen_title("SOUND");
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/title_48.pbm", r.pbm());
	});
	it("draws the title in style.title_font rather than a fixed face", _ {
		// Spec B section 6 specifies title() as style.title_font. Both shipped profiles set
		// MetricBold9, so the example above cannot tell a styled title from a hardcoded one --
		// this one gives the style something to prove.
		uitest::Rig48 r;
		Style big = kDelugeStyle48;
		big.title_font = FontId::MetricBold13;
		OledFrame f = root_frame(r.canvas, big, r.ctx);
		title(f, "SOUND");
		// Legacy equivalent: draw_string("SOUND", 0, 1 + 5, kTextBigSpacingX, kTextBigSizeY).
		// The 13px face inks rows 6..18; the 9px face stops at 14, so row 16 is the tell.
		expect(r.lit(1, 16)).to_be_true();
		expect(r.lit(1, 9)).to_be_true();
		expect(r.lit(0, 17)).to_be_true();           // separator still at kTitleSeparatorY + 5

		r.reference.draw_string("SOUND", 0, 6, 11, 13);
		r.reference.draw_horizontal_line(17, 0, 127);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});
	it("aligns text left, centre and right within a rect", _ {
		uitest::Rig48 r;
		text(r.frame, Rect{0, 0, 128, 9}, "L", {FontId::Apple7, Align::Left});
		text(r.frame, Rect{0, 10, 128, 9}, "C", {FontId::Apple7, Align::Centre});
		text(r.frame, Rect{0, 20, 128, 9}, "R", {FontId::Apple7, Align::Right});
		expect(measure("C", FontId::Apple7).w).to_equal(6);

		// Left: drawString at the rect's left edge, so the 'L' stem is column 0. The rect's
		// y = 0 is canvas row 5, but kApple7Up's y_adjust = 1 (gfx/oled/font.cpp:13, applied at
		// canvas.cpp:492) puts the first inked row at 6 -- row 5 stays dark, which is exactly
		// what distinguishes the height-9 Apple entry from the height-7 one.
		expect(r.lit(0, 6)).to_be_true();
		expect(r.lit(0, 5)).to_be_false();
		expect(r.lit(4, 12)).to_be_true();           // the foot of the L ends at column 4
		expect(r.lit(5, 12)).to_be_false();
		// Centre: drawStringCentred (canvas.cpp:381) puts x at 128/2 - (6 >> 1) = 61.
		expect(r.lit(61, 18)).to_be_true();
		expect(r.lit(60, 18)).to_be_false();
		// Right: drawStringAlignRight (canvas.cpp:468) puts x at 128 - 6 = 122.
		expect(r.lit(122, 26)).to_be_true();
		expect(r.lit(126, 27)).to_be_true();
		expect(r.lit(121, 26)).to_be_false();

		r.reference.draw_string("L", 0, 5, 6, 9);
		r.reference.draw_string("C", 61, 15, 6, 9);
		r.reference.draw_string("R", 122, 25, 6, 9);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_align_48.pbm", r.pbm());
	});
	it("centres an odd-width string the way drawStringCentred rounds it", _ {
		// "SOUND" in MetricBold9 is 45px, an odd width -- the case where `centre - (w >> 1)`
		// (canvas.cpp:381, x = 42) and `(rect.w - w) / 2` (x = 41) disagree. The 'S' glyph's
		// own first column is blank, so the leftmost ink lands at 43, not 42.
		uitest::Rig48 r;
		text(r.frame, Rect{0, 30, 128, 10}, "SOUND", {FontId::MetricBold9, Align::Centre});
		expect(r.lit(43, 37)).to_be_true();
		expect(r.lit(42, 37)).to_be_false();

		r.reference.draw_string_centred("SOUND", 35, 9, 10);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});
	it("shrinks a long centred string like drawStringCentredShrinkIfNecessary", _ {
		uitest::Rig48 r;
		text(r.frame, Rect{0, 20, 128, 20}, "THIS TITLE IS FAR TOO LONG TO FIT",
		     {FontId::MetricBold20, Align::Centre, true});
		// canvas.cpp:429-457: 33 chars over 128px gives maxTextWidth 3, so newHeight =
		// (20*3)/18 = 3 -> the 5px font (:444-446), textWidth 3 (:448), pixelY += (20-5) >> 1
		// = 7 (:450-451) and pixelX = (128 - 3*33) >> 1 = 14 (:456). Canvas rows 32..36 (local
		// 27 plus visible_top 5), leftmost ink at column 14.
		expect(r.lit(14, 32)).to_be_true();
		expect(r.lit(13, 32)).to_be_false();
		expect(r.lit(14, 31)).to_be_false();         // nothing above the shrunken band
		expect(r.lit(14, 37)).to_be_false();         // nor below it

		r.reference.draw_string_centred_shrink_if_necessary("THIS TITLE IS FAR TOO LONG TO FIT", 25, 18, 20);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_shrink_48.pbm", r.pbm());
	});
	it("uses the 13px font's own 11px advance when deciding to shrink", _ {
		// The C2 regression, isolated. "1234567890123" is 13 chars, so maxTextWidth = 128/13
		// = 9 (canvas.cpp:429). With the correct textWidth 11 that trips the shrink test at
		// :430; with the title font's 9 it does not (9 > 9 is false) and the string renders
		// full size. newHeight = (13*9)/11 = 10 -> the 10 rung (:438-440), textWidth 9,
		// pixelY += (13-10) >> 1 = 1, pixelX = (128 - 9*13) >> 1 = 5.
		uitest::Rig48 r;
		text(r.frame, Rect{0, 10, 128, 13}, "1234567890123", {FontId::MetricBold13, Align::Centre, true});
		expect(r.lit(7, 17)).to_be_true();           // inside the shrunken 9-row band (16..24)
		expect(r.lit(9, 15)).to_be_false();          // the unshrunken render inks row 15 here
		expect(r.lit(9, 25)).to_be_false();          // ...and row 25

		r.reference.draw_string_centred_shrink_if_necessary("1234567890123", 15, 11, 13);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});
	it("truncates to fit like drawStringCentered(startX ... totalWidth)", _ {
		uitest::Rig48 r;
		// canvas.cpp:385-418. Trim from the end while the width is >= total_width - 3 (:401):
		// the Apple face is 6px per character at height 9, and a 60px slot holds 9 of them
		// (54 < 57 <= 60). Padding is (60 - 54) / 2 = 3 exactly (:409-415), so x = 10 + 3.
		text(r.frame, Rect{10, 20, 60, 9}, "PORTAMENTO ON THE SLOT", {FontId::Apple7, Align::Centre, false, true});
		expect(r.lit(13, 26)).to_be_true();
		expect(r.lit(12, 26)).to_be_false();
		expect(r.lit(70, 26)).to_be_false();         // nothing past the slot: it really trimmed

		// The 24-character cap (:389-396) is the other half. 30 characters of the 5px font
		// would fit a 128px rect without trimming, so only the cap can shorten this one:
		// 24 chars start at x = 16, all 30 would start near x = 4.
		text(r.frame, Rect{0, 5, 128, 5}, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123", {FontId::Px5, Align::Centre, false, true});
		expect(r.lit(16, 11)).to_be_true();
		expect(r.lit(8, 11)).to_be_false();

		r.reference.draw_string_centered("PORTAMENTO ON THE SLOT", 10, 25, 6, 9, 60);
		r.reference.draw_string_centered("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123", 0, 10, 4, 5, 128);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_truncate_48.pbm", r.pbm());
	});
	it("renders the same widgets on a 128x64 panel", _ {
		uitest::Rig64 r;
		title(r.frame, "SOUND");
		text(r.frame, Rect{0, 20, 128, 9}, "C", {FontId::Apple7, Align::Centre});
		// visible_top is 0 here, so the same two constants land on rows 1 and 12 rather than
		// 6 and 17 -- the whole point of taking the bezel out of the widget.
		expect(r.lit(2, 1)).to_be_true();
		expect(r.lit(0, 12)).to_be_true();           // separator at 12 + 0
		expect(r.lit(127, 12)).to_be_true();
		expect(r.lit(0, 11)).to_be_false();
		expect(r.lit(61, 23)).to_be_true();          // the centred 'C', x = 61, y = 20 + y_adjust
		expect(r.lit(60, 23)).to_be_false();
		expect(r.lit(61, 20)).to_be_false();         // y_adjust again: the rect's own top row is clear
		expect(r.frame.bounds()).to_equal(Rect{0, 0, 128, 64});

		r.reference.draw_screen_title("SOUND");
		r.reference.draw_string("C", 61, 20, 6, 9);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/title_text_64.pbm", r.pbm());
	});
	it("aligns and shrinks on a 128x64 panel too", _ {
		uitest::Rig64 r;
		text(r.frame, Rect{0, 4, 128, 9}, "L", {FontId::Apple7, Align::Left});
		text(r.frame, Rect{0, 16, 128, 9}, "R", {FontId::Apple7, Align::Right});
		text(r.frame, Rect{0, 30, 128, 20}, "THIS TITLE IS FAR TOO LONG TO FIT",
		     {FontId::MetricBold20, Align::Centre, true});
		expect(r.lit(0, 5)).to_be_true();            // y_adjust: rect y 4 inks from 5
		expect(r.lit(0, 4)).to_be_false();
		expect(r.lit(122, 17)).to_be_true();
		expect(r.lit(121, 17)).to_be_false();
		expect(r.lit(14, 37)).to_be_true();          // shrink: 30 + ((20-5) >> 1) = 37, x = 14
		expect(r.lit(13, 37)).to_be_false();

		r.reference.draw_string("L", 0, 4, 6, 9);
		r.reference.draw_string("R", 122, 16, 6, 9);
		r.reference.draw_string_centred_shrink_if_necessary("THIS TITLE IS FAR TOO LONG TO FIT", 30, 18, 20);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_align_shrink_64.pbm", r.pbm());
	});
	it("draws a title without its separator when asked", _ {
		uitest::Rig48 r;
		title(r.frame, "SOUND", false);
		expect(r.lit(2, 6)).to_be_true();
		expect(r.lit(0, 17)).to_be_false();
		r.reference.draw_screen_title("SOUND", false);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});
});
DELUGE_SPEC(ui_text_spec, text_widgets)
