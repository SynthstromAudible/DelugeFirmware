#include "../gfx/support/golden.h"
#include "gfx/oled/icons.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/core/style.h"
#include "ui/oled/list.h"

#include "cppspec.hpp"

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::BorderRadius;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;
namespace icons = deluge::gfx::oled::icons;

// Every scene below pins the row geometry twice over. First as individual canvas coordinates
// read off the *legacy* renderers -- `MenuItem::drawItemsForOled` (menu_item.cpp:47-66) and
// `Submenu::drawSubmenuItemsForOled` (submenu.cpp:98-127) -- so a port that merely agrees with
// itself fails. Second, for the three plain-geometry scenes, as byte-for-byte equality with the
// legacy call sequence replayed onto a second canvas of the same geometry through the raw
// gfx Canvas, with every legacy magic number spelled as a literal (6/9 for the Apple font's
// textWidth/textHeight, 14/15 for baseY, 118 for the icon start) rather than borrowed from the
// constants the port itself lays out with.
//
// Deviations from the task brief's draft assertions, all resolved in the legacy code's favour:
//
//  D1. The brief expects `lit(0, 19)` and `lit(127, 27)` -- the highlight's top-left and
//      bottom-right *corners* -- to be lit. They are not: legacy highlights a row with
//      `invertLeftEdgeForMenuHighlighting(0, OLED_MAIN_WIDTH_PIXELS, yPixel, yPixel + 8)`
//      (menu_item.cpp:61, submenu.cpp:122), whose inverting branch is
//      `invertAreaRounded(...)` (oled_canvas/canvas.cpp:26-33), which forwards
//      `roundedCornersEnabled` (oled_canvas/canvas.h:136-138) and, when set, clears the four
//      corner pixels back off (gfx/oled/canvas.cpp:680-700). `kDelugeStyle48.rounded` is true,
//      so the corners are dark and the pixels one row in are lit. Asserted both ways below.
//  D2. The brief's `list` draws a scrollbar per spec B section 6's catalogue row. Neither
//      legacy menu renderer draws one -- there is no scrollbar anywhere in the firmware to
//      port -- so `list` draws none and `scrollbar` is specified on its own, as spec B's own
//      `scrollbar` row ("for non-list views") already implies.
//  D5. The brief's `list` took no rect and painted the whole frame. Spec B section 6 declares
//      `list(f, rect, ListState&, span<ListRow>)`, and the spec wins. `menu_rect(f)` returns
//      the rect a legacy menu occupies, and the first scene below asserts that the resulting
//      pixels are identical to the brief's whole-frame layout.

// clang-format off
describe list_w("ui::oled::list", $ {
	it("puts a legacy menu's rect at the legacy base-y", _ {
		// menu_item.cpp:50 -- baseY is 14 on a 48px panel and 15 on a 64px one; the
		// `+= OLED_MAIN_TOPMOST_PIXEL` on the next line is the frame's origin, so these are
		// frame-local. Both panels leave room for exactly style.menu_rows rows below it.
		uitest::Rig48 r48;
		const Rect m48 = menu_rect(r48.frame);
		expect(m48.x).to_equal(0);
		expect(m48.y).to_equal(14);
		expect(m48.w).to_equal(128);
		expect(m48.h).to_equal(29);            // 43 visible rows - 14; 29 / 9 == 3 == menu_rows
		uitest::Rig64 r64;
		const Rect m64 = menu_rect(r64.frame);
		expect(m64.y).to_equal(15);
		expect(m64.h).to_equal(49);            // 64 - 15; 49 / 9 == 5 == menu_rows
	});

	it("draws three rows at the legacy positions with the first highlighted", _ {
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}, {"FOUR"}, {"FIVE"}};
		uitest::Rig48 r; ListState st; ScrollText sc;
		list(r.frame, menu_rect(r.frame), st, rows, sc);
		expect(st.rows_last).to_equal(int16_t(5));
		expect(st.cursor).to_equal(int16_t(0));
		expect(st.scroll).to_equal(int16_t(0));

		// Legacy baseY for a 48px panel is 14 (menu_item.cpp:50) plus OLED_MAIN_TOPMOST_PIXEL
		// = 5 (board_config.h:64, carried by the frame's origin), so row 0 occupies canvas
		// rows 19..27 and rows are kTextSpacingY = 9 apart: 19, 28, 37.
		// The highlight spans the full width, minus the four rounded corners (D1).
		expect(r.lit(0, 18)).to_be_false();    // the row above the first one is untouched
		expect(r.lit(0, 19)).to_be_false();    // top-left corner, rounded off
		expect(r.lit(127, 19)).to_be_false();  // top-right corner
		expect(r.lit(0, 27)).to_be_false();    // bottom-left corner
		expect(r.lit(127, 27)).to_be_false();  // bottom-right corner
		expect(r.lit(0, 20)).to_be_true();     // ... but the edges themselves are lit
		expect(r.lit(0, 26)).to_be_true();
		expect(r.lit(127, 20)).to_be_true();
		expect(r.lit(127, 26)).to_be_true();
		expect(r.lit(0, 28)).to_be_false();    // and row 1 is not highlighted

		// Replay the legacy sequence: drawString(label, kTextSpacingX, yPixel, kTextSpacingX,
		// kTextSpacingY) per row, then invertLeftEdgeForMenuHighlighting on the selected one.
		const char* labels[] = {"ONE", "TWO", "THREE"};
		for (int o = 0; o < 3; o++) {
			const int y = 14 + 5 + o * 9;
			r.reference.draw_string(labels[o], 6, y, 6, 9);
			if (o == 0) {
				r.reference.invert_area_rounded(0, 128, y, y + 8, BorderRadius::Small, true);
			}
		}
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/list_first_48.pbm", r.pbm());
	});

	it("puts the same pixels on the panel through menu_rect as through the whole frame (D5)", _ {
		// The rect parameter is new; on the legacy rect it must change nothing. Rendered here
		// against a rect spelled out in legacy literals rather than through menu_rect, so the
		// helper cannot mask a shift.
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}, {"FOUR"}, {"FIVE"}};
		uitest::Rig48 a; ListState sa; ScrollText ca;
		list(a.frame, menu_rect(a.frame), sa, rows, ca);
		uitest::Rig48 b; ListState sb; ScrollText cb;
		list(b.frame, Rect{0, 14, 128, 29}, sb, rows, cb);
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("lays a list out inside an offset rect", _ {
		// Same widget, arbitrary rect: rows start at rect.y, labels at rect.x + kTextSpacingX,
		// the highlight spans only rect, and rect.h caps the visible count below menu_rows.
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}, {"FOUR"}, {"FIVE"}};
		uitest::Rig48 r; ListState st; ScrollText sc;
		list(r.frame, Rect{20, 10, 60, 18}, st, rows, sc);   // 18 / 9 == 2 rows, not 3
		expect(r.lit(20, 15)).to_be_false();  // canvas y 15 = local 10: top-left corner
		expect(r.lit(20, 16)).to_be_true();   // left edge of the highlight
		expect(r.lit(79, 16)).to_be_true();   // right edge, at rect.right() - 1
		expect(r.lit(80, 16)).to_be_false();  // nothing outside the rect
		expect(r.lit(19, 16)).to_be_false();
		expect(r.lit(20, 24)).to_be_false();  // bottom-left corner, local y 19
		for (int y = 33; y <= 41; y++) {      // the third row would start at local 28
			expect(r.lit(20, y)).to_be_false();
		}
	});

	it("moves the cursor with turn, clamps, and keeps it visible", _ {
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}, {"FOUR"}, {"FIVE"}};
		uitest::Rig48 r; ListState st; ScrollText sc;
		const Rect m = menu_rect(r.frame);
		list(r.frame, m, st, rows, sc);
		r.input.turn = 4; Response resp = list(r.frame, m, st, rows, sc);
		expect(resp.changed).to_be_true(); expect(st.cursor).to_equal(int16_t(4));
		expect(st.scroll).to_equal(int16_t(2));     // rows 2,3,4 visible; cursor on the last row
		r.input.turn = 3; Response again = list(r.frame, m, st, rows, sc);
		expect(st.cursor).to_equal(int16_t(4));     // clamped against rows_last, no wrap
		expect(again.changed).to_be_false();

		// The cursor is now on the *last* visible row: canvas rows 37..45 highlighted, the
		// first two rows plain.
		expect(r.lit(0, 20)).to_be_false();
		expect(r.lit(0, 29)).to_be_false();
		expect(r.lit(0, 38)).to_be_true();
		expect(r.lit(127, 44)).to_be_true();
		expect(r.lit(0, 37)).to_be_false();  // rounded corner again (D1)
		expect(r.lit(0, 45)).to_be_false();
		expect(r.lit(0, 46)).to_be_false();  // nothing below the last row

		// The windowed render replayed the legacy way, which for this state is
		// drawItemsForOled(options, selectedOption = pos = 2, offset = 2) -- the case where
		// window_start returns a non-zero origin. Three successive list calls have landed on
		// this canvas; each frame clears its own row bands, so the result must equal one clean
		// legacy render of the final window.
		const char* labels[] = {"THREE", "FOUR", "FIVE"};
		for (int o = 0; o < 3; o++) {
			const int y = 14 + 5 + o * 9;
			r.reference.draw_string(labels[o], 6, y, 6, 9);
			if (o == 2) {
				r.reference.invert_area_rounded(0, 128, y, y + 8, BorderRadius::Small, true);
			}
		}
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/list_last_48.pbm", r.pbm());
	});

	it("keeps the cursor visible at every position, per Selection's pos/tail rule", _ {
		// selection.cpp:9-29 with kOLEDMenuNumOptionsVisible == 3: the cursor sits in the
		// middle when it can, and is pinned to the near end when it cannot.
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}, {"FOUR"}, {"FIVE"}};
		const int16_t want_scroll[] = {0, 0, 1, 2, 2};
		for (int16_t c = 0; c < 5; c++) {
			uitest::Rig48 r; ListState st; ScrollText sc;
			st.cursor = c; st.rows_last = 5;
			list(r.frame, menu_rect(r.frame), st, rows, sc);
			expect(st.scroll).to_equal(want_scroll[c]);
			expect(st.cursor - st.scroll >= 0).to_be_true();
			expect(st.cursor - st.scroll < 3).to_be_true();  // always inside the window
		}
	});

	it("windows a list shorter than menu_rows without scrolling it", _ {
		const ListRow two[] = {{"ONE"}, {"TWO"}};
		for (int16_t c = 0; c < 2; c++) {
			uitest::Rig48 r; ListState st; ScrollText sc;
			st.cursor = c; st.rows_last = 2;
			list(r.frame, menu_rect(r.frame), st, two, sc);
			expect(st.scroll).to_equal(int16_t(0));
		}
		// Only two bands are drawn; the third row's band (canvas 37..45) stays empty.
		uitest::Rig48 r; ListState st; ScrollText sc;
		list(r.frame, menu_rect(r.frame), st, two, sc);
		for (int y = 37; y <= 45; y++) {
			expect(r.lit(0, y)).to_be_false();
			expect(r.lit(64, y)).to_be_false();
		}
	});

	it("does not latch select_pressed: the caller supplies edges (ui/core/input.h)", _ {
		// `select_pressed` is an *edge* -- true only on the tick the button went down -- and the
		// widget deliberately does no de-duplication of its own. Two consecutive ticks with the
		// flag still set therefore report `entered` twice. That is the contract, not a bug: a
		// shell that lowered a level here would enter a submenu once per frame, and this case is
		// what makes the requirement visible to whoever writes that shell.
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}};
		uitest::Rig48 r; ListState st; ScrollText sc;
		r.input.select_pressed = true;
		expect(list(r.frame, menu_rect(r.frame), st, rows, sc).entered).to_be_true();
		expect(list(r.frame, menu_rect(r.frame), st, rows, sc).entered).to_be_true();  // still held
		// The caller clearing the flag is the only thing that stops it -- as the shell does when
		// the button has not just gone down.
		r.input.select_pressed = false;
		expect(list(r.frame, menu_rect(r.frame), st, rows, sc).entered).to_be_false();
	});

	it("reports entered on select press, and never on a non-selectable row", _ {
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}, {"FOUR"}, {"FIVE"}};
		uitest::Rig48 r; ListState st; ScrollText sc;
		r.input.select_pressed = true;
		expect(list(r.frame, menu_rect(r.frame), st, rows, sc).entered).to_be_true();

		// Legacy has no drawn-but-unselectable row: Submenu filters irrelevant items out of the
		// window entirely (submenu.cpp:60-76) and selectEncoderAction only ever consumes a
		// detent on a relevant one, reverting to the last relevant item at the ends
		// (submenu.cpp:132-176). ListRow::selectable reproduces that scan for rows the caller
		// does want drawn.
		const ListRow mixed[] = {{"HEAD", {}, nullptr, false}, {"ONE"}, {"GAP", {}, nullptr, false}, {"TWO"}};
		uitest::Rig48 r2; ListState st2; ScrollText sc2;
		st2.cursor = 0; st2.rows_last = 4;                 // parked on the unselectable header
		r2.input.select_pressed = true;
		expect(list(r2.frame, menu_rect(r2.frame), st2, mixed, sc2).entered).to_be_false();

		uitest::Rig48 r3; ListState st3; ScrollText sc3;
		const Rect m3 = menu_rect(r3.frame);
		st3.cursor = 1; st3.rows_last = 4;
		r3.input.turn = 1;                                  // one detent skips "GAP" entirely
		list(r3.frame, m3, st3, mixed, sc3);
		expect(st3.cursor).to_equal(int16_t(3));
		r3.input.turn = 1;                                  // ... and stops at the end, no wrap
		list(r3.frame, m3, st3, mixed, sc3);
		expect(st3.cursor).to_equal(int16_t(3));
	});

	it("bounds turn by the row count the previous frame saw", _ {
		const ListRow five[] = {{"ONE"}, {"TWO"}, {"THREE"}, {"FOUR"}, {"FIVE"}};
		const ListRow two[] = {{"ONE"}, {"TWO"}};
		uitest::Rig48 r; ListState st; ScrollText sc;
		const Rect m = menu_rect(r.frame);
		list(r.frame, m, st, five, sc);
		expect(st.rows_last).to_equal(int16_t(5));
		// The list shrank to two rows this frame; the cursor may not be walked past what the
		// span actually holds, and rows_last is then republished as the new count.
		r.input.turn = 4;
		list(r.frame, m, st, two, sc);
		expect(st.cursor).to_equal(int16_t(1));
		expect(st.rows_last).to_equal(int16_t(2));
	});

	it("draws the left-edge rule instead of an inverted row when invert_highlight is off", _ {
		// canvas.cpp:26-29: the non-inverting branch of invertLeftEdgeForMenuHighlighting is
		// draw_vertical_line(xMin, startY, endY) -- one column, no rounding, nothing else.
		const ListRow rows[] = {{"ONE"}, {"TWO"}, {"THREE"}};
		uitest::Rig48 r;
		Style plain = kDelugeStyle48;
		plain.invert_highlight = false;
		OledFrame f = root_frame(r.canvas, plain, r.input, r.gate);
		ListState st; ScrollText sc;
		list(f, menu_rect(f), st, rows, sc);
		for (int y = 19; y <= 27; y++) {
			expect(r.lit(0, y)).to_be_true();     // the rule, corners included
		}
		expect(r.lit(1, 20)).to_be_false();       // and no full-row inversion behind it
		expect(r.lit(127, 23)).to_be_false();
		expect(r.lit(0, 18)).to_be_false();
		expect(r.lit(0, 28)).to_be_false();
	});

	it("scrolls a selected label that overflows its row, under an intact highlight (D3)", _ {
		// The D3 ruling -- label drawn first, whole row inverted after -- only has teeth once
		// the label actually scrolls. This is the case: 41 chars at the Apple font's 6px
		// advance is 246px against a 122px box, so scroll_text takes its scrolling branch
		// (scroll_text.h) instead of the "fits" early return.
		const char* longest = "A VERY LONG MENU ROW LABEL THAT OVERFLOWS";
		const ListRow rows[] = {{longest}, {"TWO"}, {"THREE"}};
		uitest::Rig48 r; ListState st; ScrollText sc;
		const Rect m = menu_rect(r.frame);
		r.input.now_ms = 0;             list(r.frame, m, st, rows, sc);  // arms the 400ms pause
		expect(sc.offset).to_equal(int16_t(0));
		r.input.now_ms = kScrollTime;   list(r.frame, m, st, rows, sc);  // first pixel step
		r.input.now_ms = kScrollTime + kScrollStepForwardMs;
		list(r.frame, m, st, rows, sc);
		expect(sc.offset > 0).to_be_true();
		expect(sc.offset).to_equal(int16_t(2));

		// The highlight is still the full, rounded row -- the point of D3. Legacy's scroller
		// *tick* path would by now have re-inverted [6, 128) with the non-rounded invertArea
		// (oled.cpp:996-997), re-lighting (127, 19) and (127, 27); this port keeps the
		// full-redraw result instead.
		expect(r.lit(0, 19)).to_be_false();   expect(r.lit(127, 19)).to_be_false();
		expect(r.lit(0, 27)).to_be_false();   expect(r.lit(127, 27)).to_be_false();
		expect(r.lit(0, 20)).to_be_true();    expect(r.lit(127, 20)).to_be_true();
		expect(r.lit(0, 26)).to_be_true();    expect(r.lit(127, 26)).to_be_true();
		expect(r.lit(0, 28)).to_be_false();   // and the next row is untouched by any of it
		expect_golden(self, "../ui/widgets/list_scrolling_48.pbm", r.pbm());
	});

	it("scrolls the same label under the left-edge rule, with legacy's clear-only +4 pad", _ {
		// The other half of D3's routing: with inversion off, scroll_text is handed
		// clear_pad_x = 4 (oled.cpp:984-989) and no highlight, and the rule stays a single
		// column.
		const char* longest = "A VERY LONG MENU ROW LABEL THAT OVERFLOWS";
		const ListRow rows[] = {{longest}, {"TWO"}, {"THREE"}};
		uitest::Rig48 r;
		Style plain = kDelugeStyle48;
		plain.invert_highlight = false;
		OledFrame f = root_frame(r.canvas, plain, r.input, r.gate);
		ListState st; ScrollText sc;
		const Rect m = menu_rect(f);
		r.input.now_ms = 0;             list(f, m, st, rows, sc);
		r.input.now_ms = kScrollTime;   list(f, m, st, rows, sc);
		r.input.now_ms = kScrollTime + kScrollStepForwardMs;
		list(f, m, st, rows, sc);
		expect(sc.offset).to_equal(int16_t(2));
		for (int y = 19; y <= 27; y++) {
			expect(r.lit(0, y)).to_be_true();     // the rule
		}
		expect(r.lit(1, 19)).to_be_false();       // no inversion anywhere behind the label
		expect(r.lit(127, 19)).to_be_false();
		expect(r.lit(127, 27)).to_be_false();
		bool label_ink = false;                   // ... but the label is really there
		for (int x = 6; x < 120; x++) {
			for (int y = 20; y <= 26; y++) {
				label_ink = label_ink || r.lit(x, y);
			}
		}
		expect(label_ink).to_be_true();
	});

	it("shows five rows on 128x64 with an icon column", _ {
		uitest::Rig64 r; ListState st; ScrollText sc;
		const ListRow with_icon[] = {{"A", {}, &icons::checkedBox}, {"B"}, {"C"}, {"D"}, {"E"}, {"F"}};
		list(r.frame, menu_rect(r.frame), st, with_icon, sc);
		expect(st.rows_last).to_equal(int16_t(6));
		expect(st.scroll).to_equal(int16_t(0));

		// Legacy baseY for a 64px panel is 15 with OLED_MAIN_TOPMOST_PIXEL 0
		// (menu_item.cpp:50), so rows sit at 15, 24, 33, 42, 51 and five of the six fit
		// (OLED_HEIGHT_CHARS - 1 == kOLEDMenuNumOptionsVisible == style.menu_rows).
		expect(r.lit(0, 14)).to_be_false();
		expect(r.lit(0, 15)).to_be_false();   // rounded corner (D1)
		expect(r.lit(0, 16)).to_be_true();
		expect(r.lit(127, 22)).to_be_true();
		expect(r.lit(0, 23)).to_be_false();   // rounded corner
		expect(r.lit(0, 24)).to_be_false();   // row 1 is not highlighted
		for (int y = 60; y < 64; y++) {       // the sixth row is off the window
			expect(r.lit(6, y)).to_be_false();
		}

		// Legacy replay: drawString clipped to endX = 128 - getSubmenuItemTypeRenderLength()
		// (= 4 + 7 + 3 = 14, menu_item.h:278) for the row that renders an icon, the icon
		// blitted at getSubmenuItemTypeRenderIconStart() = 128 - 7 - 3 = 118 (menu_item.h:280),
		// then the selected row inverted.
		const char* labels[] = {"A", "B", "C", "D", "E"};
		for (int o = 0; o < 5; o++) {
			const int y = 15 + o * 9;
			const int end_x = (o == 0) ? 128 - 14 : 128;
			r.reference.draw_string(labels[o], 6, y, 6, 9, 0, end_x);
			if (o == 0) {
				r.reference.draw_graphic_multi_line(icons::checkedBox.data, 118, y, 7, 8, 1);
				r.reference.invert_area_rounded(0, 128, y, y + 8, BorderRadius::Small, true);
			}
		}
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/list_icons_64.pbm", r.pbm());
	});

	it("puts the icon column at the legacy x on an unselected row", _ {
		// Same geometry, cursor parked elsewhere, so the icon's own pixels can be read
		// straight off the canvas instead of through the selected row's inversion. All three
		// menu icons are exercised: their bitmaps are the legacy OLED::checkedBoxIcon /
		// uncheckedBoxIcon / submenuArrowIcon (oled_icons.cpp:130, :140, :150).
		uitest::Rig64 r; ListState st; ScrollText sc;
		const ListRow with_icons[] = {{"A"},
		                              {"B", {}, &icons::checkedBox},
		                              {"C", {}, &icons::uncheckedBox},
		                              {"D", {}, &icons::submenuArrow},
		                              {"E"},
		                              {"F"}};
		list(r.frame, menu_rect(r.frame), st, with_icons, sc);
		// Rows 1/2/3 span canvas 24..32, 33..41 and 42..50; each icon is 7 columns from
		// x = 118 and 8 rows tall, bit 0 of each column byte being its top row.
		// checkedBox column 0 is 0b11111110: y+0 off, y+1..y+7 on.
		expect(r.lit(118, 24)).to_be_false();
		for (int y = 25; y <= 31; y++) {
			expect(r.lit(118, y)).to_be_true();
		}
		expect(r.lit(117, 25)).to_be_false();   // nothing left of the icon start
		expect(r.lit(125, 25)).to_be_false();   // ... nor right of its 7th column
		// uncheckedBox column 2 is 0b10000010 -- a hollow box, unlike checkedBox's 0b10011010.
		expect(r.lit(120, 33)).to_be_false();
		expect(r.lit(120, 34)).to_be_true();
		expect(r.lit(120, 36)).to_be_false();
		expect(r.lit(120, 40)).to_be_true();
		// checkedBox's column 2 is 0b10011010 -- lit at the same offset (+3) where
		// uncheckedBox's 0b10000010 is hollow, which is the whole difference between them.
		expect(r.lit(120, 27)).to_be_true();
		// submenuArrow columns 0 and 1 are blank; column 2 is 0b01000100.
		for (int y = 42; y <= 49; y++) {
			expect(r.lit(118, y)).to_be_false();
			expect(r.lit(119, y)).to_be_false();
		}
		expect(r.lit(120, 44)).to_be_true();
		expect(r.lit(120, 48)).to_be_true();
		expect(r.lit(120, 45)).to_be_false();
		for (int y = 51; y <= 59; y++) {        // and no icon on the rows that declared none
			expect(r.lit(118, y)).to_be_false();
		}
	});

	it("right-aligns a row value ahead of the icon column", _ {
		uitest::Rig48 r; ListState st; ScrollText sc;
		const ListRow rows[] = {{"ONE"}, {"TWO", "ON"}, {"THREE"}};
		list(r.frame, menu_rect(r.frame), st, rows, sc);
		// No icon on that row, so the value's right edge is the rect's: drawStringAlignRight
		// puts a 2-character Apple7 run (advance 6px each, trailing spacing included) at
		// x = 128 - 12 = 116. Row 1 is canvas 28..36.
		bool in_value_box = false, right_of_label = false;
		for (int y = 28; y <= 36; y++) {
			for (int x = 116; x < 128; x++) {
				in_value_box = in_value_box || r.lit(x, y);
			}
			for (int x = 30; x < 116; x++) { // past "TWO", before the value: empty
				right_of_label = right_of_label || r.lit(x, y);
			}
		}
		expect(in_value_box).to_be_true();
		expect(right_of_label).to_be_false();
	});

	it("stacks a value and an icon on the same row", _ {
		uitest::Rig48 r; ListState st; ScrollText sc;
		const ListRow rows[] = {{"ONE"}, {"TWO", "ON", &icons::checkedBox}, {"THREE"}};
		list(r.frame, menu_rect(r.frame), st, rows, sc);
		// content_end = 128 - 14 = 114 for the icon (menu_item.h:278), so the 12px value ends
		// there: columns 102..113. The icon still sits at 118 (menu_item.h:280), and the 4px
		// legacy gap between the two stays empty.
		bool in_value_box = false;
		for (int y = 28; y <= 36; y++) {
			for (int x = 102; x < 114; x++) {
				in_value_box = in_value_box || r.lit(x, y);
			}
		}
		expect(in_value_box).to_be_true();
		for (int y = 28; y <= 36; y++) {
			for (int x = 114; x < 118; x++) {
				expect(r.lit(x, y)).to_be_false();  // the gap
			}
		}
		expect(r.lit(118, 28)).to_be_false();       // checkedBox column 0, bit 0
		expect(r.lit(118, 29)).to_be_true();
	});

	it("keeps an over-wide value from crossing the label origin", _ {
		// New column, no legacy counterpart: a value longer than the row must not push the
		// label box negative. Clamped at rect.x + kTextSpacingX.
		uitest::Rig48 r; ListState st; ScrollText sc;
		const ListRow rows[] = {{"ONE"}, {"TWO", "A VALUE FAR TOO WIDE FOR ANY ROW ON THIS PANEL"}, {"THREE"}};
		list(r.frame, menu_rect(r.frame), st, rows, sc);
		for (int y = 28; y <= 36; y++) {
			expect(r.lit(0, y)).to_be_false();      // nothing left of the label origin
			expect(r.lit(5, y)).to_be_false();
		}
	});

	it("draws a scrollbar track and thumb", _ {
		uitest::Rig48 r;
		// Local rect {124, 14, 4, 27} -> canvas rows 19..45, columns 124..127.
		scrollbar(r.frame, Rect{124, 14, 4, 27}, 0, 3, 5);
		for (int y = 19; y <= 45; y++) {
			expect(r.lit(127, y)).to_be_true();          // 1px track down the right edge
		}
		// thumb height = max(2, 27 * 3 / 5) = 16 rows, from canvas 19 to 34, rect.w wide.
		expect(r.lit(124, 19)).to_be_true();
		expect(r.lit(124, 34)).to_be_true();
		expect(r.lit(124, 35)).to_be_false();
		expect(r.lit(123, 19)).to_be_false();
		expect(r.lit(124, 46)).to_be_false();

		uitest::Rig48 r2;
		scrollbar(r2.frame, Rect{124, 14, 4, 27}, 2, 3, 5); // thumb start = 14 + 27*2/5 = 24
		expect(r2.lit(124, 28)).to_be_false();
		expect(r2.lit(124, 29)).to_be_true();
		expect(r2.lit(124, 44)).to_be_true();
		expect(r2.lit(124, 45)).to_be_false();

		uitest::Rig48 r3;                                   // degenerate: everything visible
		scrollbar(r3.frame, Rect{124, 14, 4, 27}, 0, 9, 1);
		expect(r3.lit(124, 19)).to_be_true();
		expect(r3.lit(124, 45)).to_be_true();
		scrollbar(r3.frame, Rect{124, 14, 4, 27}, 0, 3, 0);  // total 0 draws nothing at all
	});
});
// clang-format on

// Not CPPSPEC_SPEC(list_w): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `ui_list_spec`, which
// does not match the `list_w` describe-block name the macro would assume. Spelled out by hand,
// as the other ui specs do, so the exported symbol matches what the generated driver
// (all_specs.cpp) declares `extern`.
int ui_list_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(list_w).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
