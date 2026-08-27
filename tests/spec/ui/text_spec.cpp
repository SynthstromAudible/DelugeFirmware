#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/oled/text.h"

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

// clang-format off
describe text_widgets("ui::oled text & title", $ {
	it("measures by glyph metrics, with the legacy text height per font", _ {
		// Apple ][ is monospaced at kTextSpacingX; the bold fonts are proportional.
		expect(measure("C", FontId::Apple7).w).to_equal(6);
		expect(measure("C", FontId::Apple7).h).to_equal(7);
		expect(measure("SOUND", FontId::MetricBold9).w).to_equal(45);
		// MetricBold9 is *selected* by height 10 (kTextTitleSizeY) though it draws 9 rows;
		// 10 is the number the legacy layout code spaces by, so it is what measure reports.
		expect(measure("SOUND", FontId::MetricBold9).h).to_equal(10);
		expect(measure("SOUND", FontId::MetricBold20).w).to_equal(96);
		expect(spacing_x_for(FontId::MetricBold9)).to_equal(9);
		expect(spacing_x_for(FontId::MetricBold20)).to_equal(18);
	});
	it("draws a title at the legacy position with its separator", _ {
		uitest::Rig48 r;
		title(r.frame, "SOUND");
		// Canvas::draw_screen_title: start_y = 1 + visible_top, separator_y = 12 + visible_top,
		// separator spanning 0..width-1. visible_top is 5 here, so rows 6 and 17.
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
	it("aligns text left, centre and right within a rect", _ {
		uitest::Rig48 r;
		text(r.frame, Rect{0, 0, 128, 9}, "L", {FontId::Apple7, Align::Left});
		text(r.frame, Rect{0, 10, 128, 9}, "C", {FontId::Apple7, Align::Centre});
		text(r.frame, Rect{0, 20, 128, 9}, "R", {FontId::Apple7, Align::Right});
		expect(measure("C", FontId::Apple7).w).to_equal(6);

		// Left: drawString at the rect's left edge, so the 'L' stem is column 0 (canvas rows
		// 5..11, the rect's y=0 plus visible_top).
		expect(r.lit(0, 5)).to_be_true();
		expect(r.lit(4, 11)).to_be_true();           // the foot of the L ends at column 4
		expect(r.lit(5, 11)).to_be_false();
		// Centre: drawStringCentred puts x at 128/2 - (6 >> 1) = 61.
		expect(r.lit(61, 17)).to_be_true();
		expect(r.lit(60, 17)).to_be_false();
		// Right: drawStringAlignRight puts x at 128 - 6 = 122.
		expect(r.lit(122, 25)).to_be_true();
		expect(r.lit(126, 26)).to_be_true();
		expect(r.lit(121, 25)).to_be_false();

		r.reference.draw_string("L", 0, 5, 6, 7);
		r.reference.draw_string("C", 61, 15, 6, 7);
		r.reference.draw_string("R", 122, 25, 6, 7);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_align_48.pbm", r.pbm());
	});
	it("centres an odd-width string the way drawStringCentred rounds it", _ {
		// "SOUND" in MetricBold9 is 45px, an odd width -- the case where `centre - (w >> 1)`
		// (legacy, x = 42) and `(rect.w - w) / 2` (x = 41) disagree. The 'S' glyph's own first
		// column is blank, so the leftmost ink lands at 43, not 42.
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
		// 33 chars over 128px gives maxTextWidth 3, so newHeight = (20*3)/18 = 3 -> the 5px
		// font, textWidth 3, pixelY += (20-5) >> 1 = 7 and pixelX = (128 - 3*33) >> 1 = 14.
		// Canvas rows 32..36 (local 27 plus visible_top 5), leftmost ink at column 14.
		expect(r.lit(14, 32)).to_be_true();
		expect(r.lit(13, 32)).to_be_false();
		expect(r.lit(14, 31)).to_be_false();         // nothing above the shrunken band
		expect(r.lit(14, 37)).to_be_false();         // nor below it

		r.reference.draw_string_centred_shrink_if_necessary("THIS TITLE IS FAR TOO LONG TO FIT", 25, 18, 20);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/text_shrink_48.pbm", r.pbm());
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
		expect(r.lit(61, 22)).to_be_true();          // the centred 'C', x = 61, y = 20
		expect(r.lit(60, 22)).to_be_false();
		expect(r.frame.bounds()).to_equal(Rect{0, 0, 128, 64});

		r.reference.draw_screen_title("SOUND");
		r.reference.draw_string("C", 61, 20, 6, 7);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/title_text_64.pbm", r.pbm());
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
// clang-format on

// Not CPPSPEC_SPEC(text_widgets): create_test_sourcelist derives the extern function name from
// this file's path relative to tests/spec (slashes become underscores), i.e. `ui_text_spec` for
// ui/text_spec.cpp, which does not match the `text_widgets` describe-block name the macro would
// otherwise assume. Spelled out by hand so the exported symbol matches what the generated driver
// (all_specs.cpp) declares `extern` (see ui/led_frame_spec.cpp for the same pattern).
int ui_text_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(text_widgets).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
