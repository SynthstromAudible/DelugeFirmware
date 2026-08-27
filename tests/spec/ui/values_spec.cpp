#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/core/state.h"
#include "ui/core/style.h"
#include "ui/oled/list.h"
#include "ui/oled/values.h"

#include "cppspec.hpp"

#include <string_view>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::kTextHugeSizeY;
using deluge::gfx::oled::kTextHugeSpacingX;
using deluge::gfx::oled::kTextSpacingX;
using deluge::gfx::oled::kTextSpacingY;
using deluge::gfx::oled::Rect;
using deluge::gfx::oled::SMALL;
namespace uitest = deluge::ui::test;

// Every widget below is pinned three ways at once. First as individual canvas coordinates read
// off the legacy renderers -- `Integer::drawInteger` / `Integer::drawPixelsForOled` /
// `IntegerWithOff::drawPixelsForOled` (gui/menu_item/integer.cpp:41-61),
// `Decimal::drawPixelsForOled` plus `OLED::setupBlink` (decimal.cpp:128-164, oled.cpp:832-850),
// `Toggle::drawPixelsForOled` (toggle.cpp:23-43) and `Selection::drawPixelsForOled`
// (selection.cpp:8-30) -- so a port that merely agrees with itself fails. Second as
// byte-for-byte equality with those legacy call sequences replayed onto a second canvas of the
// same geometry through the raw gfx Canvas, with every legacy magic number spelled as a literal
// (18/20 for the huge font's textWidth/textHeight, 9 for the half-width period, 20 and 41/42 for
// Decimal's canvas-absolute rows, 14/15 for the menu base-y, 6/9 for the Apple font) rather than
// borrowed from the constants the port itself lays out with. Third, for one scene per widget, as
// a golden PBM. Both shipped geometries are covered for each widget.
//
// Deviations from the task brief's draft assertions, all resolved in the legacy code's favour:
//
//  D1. The brief has `toggle` flip on a turn ("turn or select toggles"). `Toggle::selectEncoderAction`
//      is `setValue(offset > 0)` (toggle.cpp:14-17): a turn *selects* the row in its own
//      direction, so turning the same way twice is idempotent.
//  D2. Nor does a select press flip it. This two-row screen only exists while the Toggle is the
//      current menu item (`Toggle::shouldEnterSubmenu()` is false, toggle.h:19, so no submenu
//      navigation can enter it), and on that branch `Toggle::selectButtonPress` returns nullptr
//      without touching the value (toggle.h:35-36) and `sound_editor.cpp:357` + `:386-389` goes up
//      a level. `toggleValue()` (toggle.h:39-43) is the *submenu row* branch. So a press reports
//      `entered` and leaves `changed` false, and the scene navigates.
//  D3. The brief describes the decimal cursor as "Blink armed" at the first frame. Legacy's
//      `setupBlink(..., shouldBlinkImmediately)` is called with false from a plain redraw and
//      only inverts the band a `kFlashTime` later (oled.cpp:840-850, via `performBlink`), so the
//      cursor is *dark* for the first 110ms and the deadline is `kFlashTime` -- both asserted.
//      Only a horizontal-encoder cursor move passes true (decimal.cpp:98-100, :163) and lights
//      it at once.
//  D4. `int_value`'s huge digits ink canvas rows 23..42 on the 48-row panel exactly as the brief
//      says (kValueY 18 + visible_top 5, kTextHugeSizeY tall) -- with row 22 and row 43 dark on
//      both sides. `decimal_value`'s do NOT: legacy Decimal draws at a canvas-absolute y of 20
//      (decimal.cpp:149) with no `OLED_MAIN_TOPMOST_PIXEL` term, so its digits ink rows 20..39 on
//      *both* panels and the cursor sits at rows 41..42 on both. Asserted at both geometries.
//  D5. Every widget takes spec B section 6's `Rect` (the brief's interfaces omitted it, as they
//      did for `list`; the spec wins on the same ground as `list.h:68-72`). The rect that
//      reproduces legacy is `f.bounds()` for `int_value`/`decimal_value` and `menu_rect(f)` for
//      `enum_value`/`toggle`; every golden below uses it, and "lays out relative to the rect"
//      pins what a different rect does -- including `decimal_value`'s y staying canvas-absolute.

// clang-format off
describe values_w("ui::oled::values", $ {

	// ---------------------------------------------------------------- int_value

	it("draws the legacy huge centred integer at both geometries", _ {
		uitest::Rig48 r;
		int32_t value = 42;
		const Response resp = int_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 128});
		expect(resp.focused).to_be_true();
		expect(resp.changed).to_be_false();

		// integer.cpp:59-61 -> drawInteger(kTextHugeSpacingX, kTextHugeSizeY, 18) ->
		// drawStringCentred(buffer, 18 + OLED_MAIN_TOPMOST_PIXEL, 18, 20) (integer.cpp:52-57).
		// 18 + 5 = 23, so the glyphs ink canvas rows 23..42 and nothing above or below.
		for (int x = 0; x < 128; x++) {
			expect(r.lit(x, 22)).to_be_false();
			expect(r.lit(x, 43)).to_be_false();
		}
		bool any_row[20] = {};
		for (int y = 23; y <= 42; y++) {
			for (int x = 0; x < 128; x++) {
				any_row[y - 23] = any_row[y - 23] || r.lit(x, y);
			}
		}
		for (bool row : any_row) {
			expect(row).to_be_true();          // every one of the 20 rows carries ink
		}
		// get_string_width_in_pixels("42", 20) is 35, so drawStringCentred starts it at
		// 64 - (35 >> 1) = 47; the '4' inks from column 48 and the '2' ends at column 79.
		expect(r.lit(56, 23)).to_be_true();
		expect(r.lit(48, 38)).to_be_true();
		expect(r.lit(79, 42)).to_be_true();
		expect(r.lit(80, 42)).to_be_false();

		r.reference.draw_string_centred("42", 18 + 5, 18, 20);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/int_value_42_48.pbm", r.pbm());

		// The 64-row panel has no hidden rows, so the same kValueY lands at canvas row 18.
		uitest::Rig64 r64;
		int32_t v64 = 42;
		int_value(r64.frame, r64.frame.bounds(), v64, Range{.min = 0, .max = 128});
		expect(r64.lit(56, 18)).to_be_true();
		expect(r64.lit(48, 33)).to_be_true();
		for (int x = 0; x < 128; x++) {
			expect(r64.lit(x, 17)).to_be_false();
			expect(r64.lit(x, 38)).to_be_false();
		}
		r64.reference.draw_string_centred("42", 18 + 0, 18, 20);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
	});

	it("draws OFF at zero only with off_at_zero", _ {
		uitest::Rig48 r;
		int32_t value = 0;
		int_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 128}, ValueStyle{.off_at_zero = true});
		// IntegerWithOff::drawPixelsForOled, integer.cpp:41-50: drawStringCentred("OFF",
		// 18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX, kTextHugeSizeY) -- same band, and the
		// unit is not appended. "OFF" is 51px wide, so it starts at 64 - 25 = 39 and inks 40..87.
		expect(r.lit(40, 32)).to_be_true();
		expect(r.lit(87, 26)).to_be_true();
		expect(r.lit(39, 32)).to_be_false();
		r.reference.draw_string_centred("OFF", 18 + 5, 18, 20);
		expect(r.pbm()).to_equal(r.reference_pbm());

		// Without the flag the same zero is a digit.
		uitest::Rig48 plain;
		int32_t zero = 0;
		int_value(plain.frame, plain.frame.bounds(), zero, Range{.min = 0, .max = 128});
		plain.reference.draw_string_centred("0", 18 + 5, 18, 20);
		expect(plain.pbm()).to_equal(plain.reference_pbm());
	});

	it("appends up to four characters of the unit", _ {
		uitest::Rig48 r;
		int32_t value = 7;
		int_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 128}, ValueStyle{.unit = "ms"});
		r.reference.draw_string_centred("7ms", 18 + 5, 18, 20);   // integer.cpp:53-56
		expect(r.pbm()).to_equal(r.reference_pbm());

		uitest::Rig48 long_unit;
		int32_t v = 7;
		int_value(long_unit.frame, long_unit.frame.bounds(), v, Range{.min = 0, .max = 128}, ValueStyle{.unit = "ABCDEFG"});
		long_unit.reference.draw_string_centred("7ABCD", 18 + 5, 18, 20); // strncat(..., 4)
		expect(long_unit.pbm()).to_equal(long_unit.reference_pbm());
	});

	it("applies edit[0] only when focused and clamps to the range", _ {
		uitest::Rig48 r;
		int32_t value = 41;
		r.input.edit[0] = 1;
		const Response up = int_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 42});
		expect(value).to_equal(42);
		expect(up.changed).to_be_true();       // integer.cpp:26 -- getValue() + offset

		const Response at_max = int_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 42});
		expect(value).to_equal(42);            // integer.cpp:27-30 clamps to maxValue
		expect(at_max.changed).to_be_false();

		int32_t low = 0;
		r.input.edit[0] = -3;
		const Response down = int_value(r.frame, r.frame.bounds(), low, Range{.min = 0, .max = 42});
		expect(low).to_equal(0);               // integer.cpp:31-36 clamps to minValue
		expect(down.changed).to_be_false();

		int32_t unfocused = 41;
		r.input.edit[0] = 1;
		const Response blurred = int_value(r.frame, r.frame.bounds(), unfocused, Range{.min = 0, .max = 42}, {}, false);
		expect(unfocused).to_equal(41);        // spec B section 3: edit[] moves the *focused* value
		expect(blurred.changed).to_be_false();
		expect(blurred.focused).to_be_false();
	});

	it("splits the two edit encoders into a coarse and a fine step", _ {
		// Decimal::getNumberEditSize (decimal.cpp:49-55) is legacy's only two-granularity
		// number edit: a coarse step and a 1-unit fine one. edit[0] takes range.step, edit[1]
		// takes 1.
		uitest::Rig48 r;
		int32_t value = 100;
		r.input.edit[0] = 1;
		int_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 1000, .step = 10});
		expect(value).to_equal(110);

		r.input.edit[0] = 0;
		r.input.edit[1] = -1;
		int_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 1000, .step = 10});
		expect(value).to_equal(109);
	});

	// ------------------------------------------------------------ decimal_value

	it("draws the legacy decimal at its canvas-absolute rows on both panels", _ {
		uitest::Rig48 r;
		int32_t value = 1234;
		int8_t edit_pos = 0;
		Blink blink;
		r.input.now_ms = 0;
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);

		// decimal.cpp:149/152/161 draw at a literal y of 20 -- canvas rows, with no
		// OLED_MAIN_TOPMOST_PIXEL term anywhere in the routine (D4). 18*4 + 9 = 81 wide,
		// so (128 - 81) >> 1 = 23: "12" at 23..58, "." at 59..67, "34" at 68..103.
		for (int x = 0; x < 128; x++) {
			expect(r.lit(x, 19)).to_be_false();
			expect(r.lit(x, 40)).to_be_false();
		}
		expect(r.lit(33, 20)).to_be_true();    // top of the '1'
		expect(r.lit(98, 20)).to_be_true();    // top of the '4'
		expect(r.lit(63, 34)).to_be_true();    // the period, half a digit wide at 59..67
		expect(r.lit(63, 20)).to_be_false();

		// setupBlink is armed but has not fired: shouldBlinkImmediately is false on a plain
		// redraw, so the band stays dark for one kFlashTime (oled.cpp:840-850) -- D3.
		for (int x = 0; x < 128; x++) {
			expect(r.lit(x, 41)).to_be_false();
			expect(r.lit(x, 42)).to_be_false();
		}
		expect(r.gate.next_deadline().has_value()).to_be_true();
		expect(*r.gate.next_deadline()).to_equal(uint32_t(kFlashTime));

		// One half-period later performBlink inverts the band: x = ourDigitStartX + 1 = 87,
		// width digitWidth - 2 = 16, rows 41..42 inclusive (decimal.cpp:163). The shell wakes on
		// the deadline and consumes it first -- RepaintGate keeps the *earliest* request until
		// needs_frame() clears it, so without this the deadline below would still read 110.
		expect(r.gate.needs_frame(kFlashTime, false, false)).to_be_true();
		r.input.now_ms = kFlashTime;
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(r.lit(86, 41)).to_be_false();
		expect(r.lit(87, 41)).to_be_true();
		expect(r.lit(102, 42)).to_be_true();
		expect(r.lit(103, 42)).to_be_false();
		expect(*r.gate.next_deadline()).to_equal(uint32_t(2 * kFlashTime));

		r.reference.draw_string("12", 23, 20, 18, 20, 0, 128, true);
		r.reference.draw_string(".", 23 + 2 * 18, 20, 9, 20, 0, 128, true);
		r.reference.draw_string("34", 23 + 2 * 18 + 9, 20, 18, 20, 0, 128, true);
		r.reference.invert_area(87, 16, 41, 42);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/decimal_value_12_34_48.pbm", r.pbm());

		// Same absolute rows on the 64-row panel, because the constants are canvas-absolute.
		uitest::Rig64 r64;
		int32_t v64 = 1234;
		int8_t pos64 = 0;
		Blink blink64;
		r64.input.now_ms = 0;
		decimal_value(r64.frame, r64.frame.bounds(), v64, Range{.min = 0, .max = 9999}, 2, pos64, blink64);
		r64.input.now_ms = kFlashTime;
		decimal_value(r64.frame, r64.frame.bounds(), v64, Range{.min = 0, .max = 9999}, 2, pos64, blink64);
		expect(r64.lit(33, 20)).to_be_true();
		expect(r64.lit(87, 41)).to_be_true();
		expect(r64.lit(102, 42)).to_be_true();
		r64.reference.draw_string("12", 23, 20, 18, 20, 0, 128, true);
		r64.reference.draw_string(".", 23 + 2 * 18, 20, 9, 20, 0, 128, true);
		r64.reference.draw_string("34", 23 + 2 * 18 + 9, 20, 18, 20, 0, 128, true);
		r64.reference.invert_area(87, 16, 41, 42);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
	});

	it("zero-pads to places + 1 digits and drops the period at places 0", _ {
		uitest::Rig48 r;
		int32_t value = 5;                     // intToString(5, buffer, 3) == "005"
		int8_t edit_pos = 0;
		Blink blink;
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 999}, 2, edit_pos, blink);
		// 18*3 + 9 = 63 wide -> starts at (128 - 63) >> 1 = 32: "0" at 32..49, "." at 50..58.
		r.reference.draw_string("0", 32, 20, 18, 20, 0, 128, true);
		r.reference.draw_string(".", 32 + 18, 20, 9, 20, 0, 128, true);
		r.reference.draw_string("05", 32 + 18 + 9, 20, 18, 20, 0, 128, true);
		expect(r.pbm()).to_equal(r.reference_pbm());

		uitest::Rig48 whole;
		int32_t v = 42;
		int8_t pos = 0;
		Blink b;
		decimal_value(whole.frame, whole.frame.bounds(), v, Range{.min = 0, .max = 99}, 0, pos, b);
		// places 0 skips the whole period branch (decimal.cpp:146-159): 18*2 = 36 wide, at 46.
		whole.reference.draw_string("42", 46, 20, 18, 20, 0, 128, true);
		expect(whole.pbm()).to_equal(whole.reference_pbm());
	});

	it("moves the cursor with the horizontal encoder and lights it at once", _ {
		uitest::Rig48 r;
		int32_t value = 1234;
		int8_t edit_pos = 0;
		Blink blink;
		r.input.now_ms = 5000;
		r.input.horizontal = -1;               // decimal.cpp:91-96: one place left
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(int(edit_pos)).to_equal(1);
		// movingCursor -> setupBlink(..., true) inverts immediately (decimal.cpp:98-100, :163):
		// the cursor is one digit (18px) left of where edit_pos 0 put it, i.e. 69..84.
		expect(r.lit(69, 41)).to_be_true();
		expect(r.lit(84, 42)).to_be_true();
		expect(r.lit(85, 41)).to_be_false();
		expect(r.lit(87, 41)).to_be_false();
		expect(*r.gate.next_deadline()).to_equal(uint32_t(5000 + kFlashTime));

		r.input.horizontal = 1;                // decimal.cpp:84-89: back to the right
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(int(edit_pos)).to_equal(0);
		expect(r.lit(87, 41)).to_be_true();

		// At the right-hand end a further positive tick does nothing (decimal.cpp:85).
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(int(edit_pos)).to_equal(0);
	});

	it("restarts the blink lit on a horizontal tick that moves nothing", _ {
		// decimal.cpp:98 sets movingCursor AFTER and OUTSIDE the if/else at :84-96, so a blocked
		// tick still reaches setupBlink(..., true) at :163 and inverts the band at once
		// (oled.cpp:840-847). Reachable in the default state: getDefaultEditPos is 0
		// (decimal.cpp:37), so a rightward tick there moves nothing.
		uitest::Rig48 r;
		int32_t value = 1234;
		int8_t edit_pos = 0;
		Blink blink;
		r.input.now_ms = 5000;
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(r.lit(87, 41)).to_be_false();   // plain redraw: dark for the first kFlashTime

		r.input.horizontal = 1;                // blocked by decimal.cpp:85 -- edit_pos is already 0
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(int(edit_pos)).to_equal(0);
		expect(r.lit(87, 41)).to_be_true();    // ... but the cursor lights anyway
		expect(r.lit(102, 42)).to_be_true();
		expect(r.lit(86, 41)).to_be_false();

		// And the other blocked branch: a negative tick when the next place up leaves the range
		// (decimal.cpp:92 -- numberEditSize * 10 <= max; 10^2 * 10 > 99).
		uitest::Rig48 small;
		int32_t v = 42;
		int8_t pos = 2;
		Blink b;
		small.input.now_ms = 5000;
		small.input.horizontal = -1;
		decimal_value(small.frame, small.frame.bounds(), v, Range{.min = 0, .max = 99}, 2, pos, b);
		expect(int(pos)).to_equal(2);
		// "042" is 3 digits + period = 63px wide, centred at x 32; edit_pos 2 puts the cursor
		// under the leading digit at 32..49, so the band is 33..48.
		expect(small.lit(33, 41)).to_be_true();
		expect(small.lit(48, 42)).to_be_true();
		expect(small.lit(32, 41)).to_be_false();
	});

	it("moves the value by 10^edit_pos per edit detent", _ {
		uitest::Rig48 r;
		int32_t value = 1234;
		int8_t edit_pos = 2;
		Blink blink;
		r.input.edit[0] = 1;
		const Response resp = decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(value).to_equal(1334);          // decimal.cpp:58 -- offset * getNumberEditSize()
		expect(resp.changed).to_be_true();

		r.input.edit[0] = 100;                 // way past the top: decimal.cpp:70-73 clamps
		const Response clamped = decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(value).to_equal(9999);
		expect(clamped.changed).to_be_true();

		const Response stuck = decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(value).to_equal(9999);
		expect(stuck.changed).to_be_false();

		// edit[1] is the fine knob and takes range.step -- note that is the opposite role step
		// plays in int_value, where it is the coarse amount (values.h says so).
		int32_t fine = 1234;
		int8_t fine_pos = 2;
		Blink fine_blink;
		r.input.edit[0] = 0;
		r.input.edit[1] = -1;
		decimal_value(r.frame, r.frame.bounds(), fine, Range{.min = 0, .max = 9999, .step = 5}, 2, fine_pos,
		              fine_blink);
		expect(fine).to_equal(1229);

		r.input.edit[1] = 0;
		int32_t unfocused = 1234;
		int8_t pos = 2;
		Blink b;
		decimal_value(r.frame, r.frame.bounds(), unfocused, Range{.min = 0, .max = 9999}, 2, pos, b, false);
		expect(unfocused).to_equal(1234);
	});

	// --------------------------------------------------------------- enum_value

	it("windows six names around the current one", _ {
		const std::string_view names[] = {"AAA", "BBB", "CCC", "DDD", "EEE", "FFF"};
		uitest::Rig48 r;
		int32_t index = 4;
		ListState st;
		ScrollText sc;
		const Response resp = enum_value(r.frame, menu_rect(r.frame), index, names, st, sc);
		expect(resp.changed).to_be_false();
		// Selection's window rule (selection.cpp:11-27) over kOLEDMenuNumOptionsVisible = 3:
		// nBefore 3, nAfter 2, pos 1 -> first visible name is index 3.
		expect(st.scroll).to_equal(int16_t(3));
		expect(st.cursor).to_equal(int16_t(4));
		expect(st.rows_last).to_equal(int16_t(6));

		// Rows at the legacy base-y (14 + OLED_MAIN_TOPMOST_PIXEL = 19), kTextSpacingY apart,
		// with the selected one inverted and its four corners rounded off.
		expect(r.lit(0, 28)).to_be_false();    // top-left corner of the highlight
		expect(r.lit(0, 29)).to_be_true();     // ... the edge one row in is lit
		expect(r.lit(127, 32)).to_be_true();
		expect(r.lit(0, 27)).to_be_false();    // the unhighlighted row above stays dark at x 0
		for (int o = 0; o < 3; o++) {
			r.reference.draw_string(names[3 + o], kTextSpacingX, 19 + 9 * o, 6, 9);
		}
		r.reference.invert_area_rounded(0, 128, 28, 36, SMALL, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/enum_value_window_48.pbm", r.pbm());

		// Five rows fit on the 64-row panel, so the same index 4 windows differently:
		// nBefore 4, nAfter 2 -> tail 2, pos 3, first visible name is index 1.
		uitest::Rig64 r64;
		int32_t i64 = 4;
		ListState st64;
		ScrollText sc64;
		enum_value(r64.frame, menu_rect(r64.frame), i64, names, st64, sc64);
		expect(st64.scroll).to_equal(int16_t(1));
		for (int o = 0; o < 5; o++) {
			r64.reference.draw_string(names[1 + o], kTextSpacingX, 15 + 9 * o, 6, 9);
		}
		r64.reference.invert_area_rounded(0, 128, 15 + 27, 15 + 27 + 8, SMALL, true);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
	});

	it("clamps by default and wraps when asked", _ {
		const std::string_view names[] = {"AAA", "BBB", "CCC", "DDD", "EEE", "FFF"};
		uitest::Rig48 r;
		ListState st;
		ScrollText sc;

		// Enumeration::wrapAround() is false by default (enumeration.cpp:12-16), so
		// enumeration.cpp:28 clamps.
		int32_t index = 5;
		r.input.turn = 1;
		const Response clamped = enum_value(r.frame, menu_rect(r.frame), index, names, st, sc);
		expect(index).to_equal(5);
		expect(clamped.changed).to_be_false();

		// enumeration.cpp:25 -- mod(nextValue, numOptions), which is positive-valued.
		const Response wrapped = enum_value(r.frame, menu_rect(r.frame), index, names, st, sc, true);
		expect(index).to_equal(0);
		expect(wrapped.changed).to_be_true();

		r.input.turn = -1;
		enum_value(r.frame, menu_rect(r.frame), index, names, st, sc, true);
		expect(index).to_equal(5);
		enum_value(r.frame, menu_rect(r.frame), index, names, st, sc);   // -1 from 5, no wrapping needed
		expect(index).to_equal(4);

		r.input.turn = 0;
		r.input.select_pressed = true;
		const Response entered = enum_value(r.frame, menu_rect(r.frame), index, names, st, sc);
		expect(entered.entered).to_be_true();
	});

	it("keeps a long list off the stack", _ {
		// Only the visible window is materialised, so a list far longer than kMaxWindowRows
		// renders (and windows) exactly as a short one does.
		std::string_view names[200];
		for (auto& n : names) {
			n = "XX";
		}
		names[100] = "HERE";
		uitest::Rig48 r;
		int32_t index = 100;
		ListState st;
		ScrollText sc;
		enum_value(r.frame, menu_rect(r.frame), index, names, st, sc);
		expect(st.scroll).to_equal(int16_t(99));
		expect(st.rows_last).to_equal(int16_t(200));
		r.reference.draw_string("XX", kTextSpacingX, 19, 6, 9);
		r.reference.draw_string("HERE", kTextSpacingX, 28, 6, 9);
		r.reference.draw_string("XX", kTextSpacingX, 37, 6, 9);
		r.reference.invert_area_rounded(0, 128, 28, 36, SMALL, true);
		expect(r.pbm()).to_equal(r.reference_pbm());

		// Both ends, where Selection's pos/tail rule switches branches (selection.cpp:21-27):
		// at the head nBefore < pos pulls pos to 0, at the tail nAfter < tail pulls the window
		// flush against the end.
		uitest::Rig48 head;
		int32_t first = 0;
		ListState head_st;
		ScrollText head_sc;
		enum_value(head.frame, menu_rect(head.frame), first, names, head_st, head_sc);
		expect(head_st.scroll).to_equal(int16_t(0));

		uitest::Rig48 tail;
		int32_t last = 199;
		ListState tail_st;
		ScrollText tail_sc;
		enum_value(tail.frame, menu_rect(tail.frame), last, names, tail_st, tail_sc);
		expect(tail_st.scroll).to_equal(int16_t(197));
		tail.reference.draw_string("XX", kTextSpacingX, 19, 6, 9);
		tail.reference.draw_string("XX", kTextSpacingX, 28, 6, 9);
		tail.reference.draw_string("XX", kTextSpacingX, 37, 6, 9);
		tail.reference.invert_area_rounded(0, 128, 37, 45, SMALL, true);
		expect(tail.pbm()).to_equal(tail.reference_pbm());
	});

	it("lays out relative to the rect spec B section 6 gives it", _ {
		// Every golden above uses the rect that reproduces legacy -- f.bounds() for the two
		// number widgets (legacy centred over the panel and measured its 18 from the panel top)
		// and menu_rect(f) for the two list widgets (legacy's base-y). A different rect moves the
		// widget by exactly that rect's offset, except decimal_value's two y constants, which are
		// canvas-absolute in legacy and stay put.
		uitest::Rig48 r;
		int32_t value = 42;
		int_value(r.frame, Rect{0, 5, 64, 38}, value, Range{.min = 0, .max = 128});
		// Centred over 64px at local y 5 + kValueY = 23, i.e. canvas row 28.
		r.reference.draw_string_centred("42", 5 + 18 + 5, 18, 20, 32);
		expect(r.pbm()).to_equal(r.reference_pbm());

		uitest::Rig48 d;
		int32_t dv = 42;
		int8_t pos = 0;
		Blink blink;
		decimal_value(d.frame, Rect{0, 0, 64, 43}, dv, Range{.min = 0, .max = 99}, 0, pos, blink);
		// x follows the rect -- (64 - 36) >> 1 = 14 -- but y is still the canvas-absolute 20
		// (decimal.cpp:149), not rect.y + anything.
		d.reference.draw_string("42", 14, 20, 18, 20, 0, 64, true);
		expect(d.pbm()).to_equal(d.reference_pbm());

		uitest::Rig48 t;
		bool on = true;
		ScrollText sc;
		toggle(t.frame, Rect{0, 20, 128, 18}, on, "OFF", "ON", sc);
		// Rows at local 20 and 29, i.e. canvas 25 and 34.
		t.reference.draw_string("OFF", kTextSpacingX, 25, kTextSpacingX, kTextSpacingY);
		t.reference.draw_string("ON", kTextSpacingX, 34, kTextSpacingX, kTextSpacingY);
		t.reference.invert_area_rounded(0, 128, 34, 42, SMALL, true);
		expect(t.pbm()).to_equal(t.reference_pbm());

		const std::string_view names[] = {"AAA", "BBB"};
		uitest::Rig48 e;
		int32_t index = 1;
		ListState st;
		ScrollText esc;
		enum_value(e.frame, Rect{0, 20, 128, 18}, index, names, st, esc);
		e.reference.draw_string("AAA", kTextSpacingX, 25, kTextSpacingX, kTextSpacingY);
		e.reference.draw_string("BBB", kTextSpacingX, 34, kTextSpacingX, kTextSpacingY);
		e.reference.invert_area_rounded(0, 128, 34, 42, SMALL, true);
		expect(e.pbm()).to_equal(e.reference_pbm());
	});

	// ------------------------------------------------------------------- toggle

	it("draws the two legacy toggle rows with the current one highlighted", _ {
		uitest::Rig48 r;
		bool value = true;
		ScrollText sc;
		const Response resp = toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc);
		expect(resp.changed).to_be_false();

		// toggle.cpp:26-42: yPixel = 14 + OLED_MAIN_TOPMOST_PIXEL = 19, rows kTextSpacingY
		// apart, drawString at kTextSpacingX, and invertLeftEdgeForMenuHighlighting(0, 128,
		// yPixel, yPixel + 8) on the selected one.
		expect(r.lit(6, 21)).to_be_true();     // the 'O' of the unhighlighted first row
		expect(r.lit(0, 28)).to_be_false();    // rounded corner of the second row's highlight
		expect(r.lit(0, 29)).to_be_true();
		expect(r.lit(127, 36)).to_be_false();  // bottom-right corner
		expect(r.lit(126, 36)).to_be_true();
		r.reference.draw_string("OFF", kTextSpacingX, 19, kTextSpacingX, kTextSpacingY);
		r.reference.draw_string("ON", kTextSpacingX, 28, kTextSpacingX, kTextSpacingY);
		r.reference.invert_area_rounded(0, 128, 28, 36, SMALL, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/toggle_on_48.pbm", r.pbm());

		uitest::Rig48 off;
		bool off_value = false;
		ScrollText off_sc;
		toggle(off.frame, menu_rect(off.frame), off_value, "OFF", "ON", off_sc);
		off.reference.draw_string("OFF", kTextSpacingX, 19, kTextSpacingX, kTextSpacingY);
		off.reference.draw_string("ON", kTextSpacingX, 28, kTextSpacingX, kTextSpacingY);
		off.reference.invert_area_rounded(0, 128, 19, 27, SMALL, true);
		expect(off.pbm()).to_equal(off.reference_pbm());

		// The 64-row panel puts the same two rows at base-y 15 (menu_item.cpp:50, toggle.cpp:26).
		uitest::Rig64 r64;
		bool v64 = true;
		ScrollText sc64;
		toggle(r64.frame, menu_rect(r64.frame), v64, "OFF", "ON", sc64);
		r64.reference.draw_string("OFF", kTextSpacingX, 15, kTextSpacingX, kTextSpacingY);
		r64.reference.draw_string("ON", kTextSpacingX, 24, kTextSpacingX, kTextSpacingY);
		r64.reference.invert_area_rounded(0, 128, 24, 32, SMALL, true);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
	});

	it("selects by turn direction and leaves the value alone on a select press", _ {
		uitest::Rig48 r;
		bool value = false;
		ScrollText sc;

		// Toggle::selectEncoderAction is setValue(offset > 0) (toggle.cpp:14-17), NOT a flip:
		// turning up twice leaves it on (D1).
		r.input.turn = 1;
		expect(toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc).changed).to_be_true();
		expect(value).to_be_true();
		expect(toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc).changed).to_be_false();
		expect(value).to_be_true();

		r.input.turn = -1;
		expect(toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc).changed).to_be_true();
		expect(value).to_be_false();

		// A select press on THIS screen does not touch the value (D2). The two-row screen only
		// exists while the Toggle is the current menu item -- Toggle::shouldEnterSubmenu() is
		// false (toggle.h:19), so no submenu navigation can enter it -- and on that branch
		// Toggle::selectButtonPress returns nullptr without calling toggleValue()
		// (toggle.h:35-36), after which sound_editor.cpp:357 + :386-389 runs goUpOneLevel().
		// The toggleValue() branch (toggle.h:39-43) fires only for a Toggle drawn as a row
		// inside a Submenu, where the parent's list row carries the checkbox icon.
		r.input.turn = 0;
		r.input.select_pressed = true;
		const bool before = value;
		const Response pressed = toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc);
		expect(value).to_equal(before);
		expect(pressed.changed).to_be_false();
		expect(pressed.entered).to_be_true();   // the scene navigates, as it does for enum_value
	});
});
// clang-format on

// Not CPPSPEC_SPEC(values_w): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `ui_values_spec`, which
// does not match the `values_w` describe-block name the macro would assume. Spelled out by hand,
// as the other ui specs do, so the exported symbol matches what the generated driver
// (all_specs.cpp) declares `extern`.
int ui_values_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(values_w).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
