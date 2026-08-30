#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/core/state.h"
#include "ui/core/style.h"
#include "ui/oled/list.h"
#include "ui/oled/values.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::BorderRadius;
using deluge::gfx::oled::kTextHugeSizeY;
using deluge::gfx::oled::kTextHugeSpacingX;
using deluge::gfx::oled::kTextSpacingX;
using deluge::gfx::oled::kTextSpacingY;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

// Every widget below is pinned three ways at once. First as individual canvas coordinates read
// off the legacy renderers -- `Integer::drawInteger` / `Integer::drawPixelsForOled` /
// `IntegerWithOff::drawPixelsForOled`, `Decimal::drawPixelsForOled` plus `OLED::setupBlink`,
// `Toggle::drawPixelsForOled` and `Selection::drawPixelsForOled` -- so a port that merely agrees
// with itself fails. Second as byte-for-byte equality with those legacy call sequences replayed
// onto a second canvas of the same geometry through the raw gfx Canvas, with every legacy magic
// number spelled as a literal (18/20 for the huge font's textWidth/textHeight, 9 for the
// half-width period, 20 and 41/42 for Decimal's canvas-absolute rows, 14/15 for the menu base-y,
// 6/9 for the Apple font) rather than borrowed from the constants the port itself lays out with.
// Third, for one scene per widget, as a golden PBM. Both shipped geometries are covered for each
// widget.
//
// Points where the legacy behaviour is easy to get wrong, so worth stating explicitly:
//
//  - `toggle` does not flip on a turn: `Toggle::selectEncoderAction` is `setValue(offset > 0)`, a
//    turn *selects* the row in its own direction, so turning the same way twice is idempotent.
//  - Nor does a select press flip it. The two-row screen only exists while the Toggle is the
//    current menu item (`Toggle::shouldEnterSubmenu()` is false, so no submenu navigation can
//    enter it), and on that branch `Toggle::selectButtonPress` returns nullptr without touching
//    the value, and the scene goes up one level. `toggleValue()` is the *submenu row* branch,
//    reached only when a Toggle is drawn as a row inside a Submenu. So a press reports `entered`
//    and leaves `changed` false, and the scene navigates.
//  - The decimal cursor is dark on the first frame: `setupBlink(..., shouldBlinkImmediately)` is
//    called with false from a plain redraw and only inverts the band one `kFlashTime` later (via
//    `performBlink`). Only a horizontal-encoder cursor move passes true and lights it at once.
//  - `int_value`'s huge digits ink canvas rows 23..42 on the 48-row panel (kValueY 18 +
//    visible_top 5, kTextHugeSizeY tall) -- with row 22 and row 43 dark on both sides.
//    `decimal_value`'s do NOT: legacy Decimal draws at a canvas-absolute y of 20 with no
//    `OLED_MAIN_TOPMOST_PIXEL` term, so its digits ink rows 20..39 on *both* panels and the cursor
//    sits at rows 41..42 on both. Asserted at both geometries.
//  - Every widget takes a `Rect`. The rect that reproduces legacy is `f.bounds()` for
//    `int_value`/`decimal_value` and `menu_rect(f)` for `enum_value`/`toggle`; every golden below
//    uses it, and "lays out relative to the rect" pins what a different rect does -- including
//    `decimal_value`'s y staying canvas-absolute.

namespace {

/// @brief `range_value`'s formatter for a plain integer -- the spec's own `intToString`, written out
///        rather than borrowed from the port, so a broken `int_to_string` cannot make both sides
///        agree.
std::string_view fmt_int(int32_t value, std::span<char> scratch) {
	char digits[12];
	size_t d = 0;
	uint32_t magnitude = value < 0 ? static_cast<uint32_t>(-static_cast<int64_t>(value)) : static_cast<uint32_t>(value);
	do {
		digits[d++] = static_cast<char>('0' + (magnitude % 10));
		magnitude /= 10;
	} while (magnitude != 0);
	size_t len = 0;
	if (value < 0 && len < scratch.size()) {
		scratch[len++] = '-';
	}
	while (d > 0 && len < scratch.size()) {
		scratch[len++] = digits[--d];
	}
	return std::string_view{scratch.data(), len};
}

/// @brief A note-letter formatter, standing in for `KeyRange::getText`. Deliberately unrelated to
///        the port's own `format_range_key`: the point of the example that uses it is that the
///        widget lays out on whatever lengths the *caller's* formatter returns.
std::string_view fmt_note(int32_t value, std::span<char> scratch) {
	static constexpr char kLetters[] = "CCDDEFFGGAAB";
	static constexpr bool kSharp[] = {false, true, false, true, false, false, true, false, true, false, true, false};
	const int32_t note = value < 0 ? 0 : (value > 11 ? 11 : value);
	size_t len = 0;
	if (len < scratch.size()) {
		scratch[len++] = kLetters[note];
	}
	if (kSharp[note] && len < scratch.size()) {
		scratch[len++] = '#';
	}
	return std::string_view{scratch.data(), len};
}

} // namespace

// clang-format off
describe values_w("ui::oled::values", $ {

	// ---------------------------------------------------------------- int_value

	it("draws the legacy huge centred integer at both geometries", _ {
		uitest::Rig48 r;
		int32_t value = 42;
		const Response resp = int_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 128});
		expect(resp.focused).to_be_true();
		expect(resp.changed).to_be_false();

		// drawInteger(kTextHugeSpacingX, kTextHugeSizeY, 18) -> drawStringCentred(buffer, 18 +
		// OLED_MAIN_TOPMOST_PIXEL, 18, 20). 18 + 5 = 23, so the glyphs ink canvas rows 23..42 and
		// nothing above or below.
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
		// IntegerWithOff::drawPixelsForOled: drawStringCentred("OFF", 18 + OLED_MAIN_TOPMOST_PIXEL,
		// kTextHugeSpacingX, kTextHugeSizeY) -- same band, and the unit is not appended. "OFF" is 51px wide, so it starts at 64 - 25 = 39 and inks 40..87.
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
		r.reference.draw_string_centred("7ms", 18 + 5, 18, 20);   // the unit is appended to the digits
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
		expect(up.changed).to_be_true();       // getValue() + offset

		const Response at_max = int_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 42});
		expect(value).to_equal(42);            // clamps to maxValue
		expect(at_max.changed).to_be_false();

		int32_t low = 0;
		r.input.edit[0] = -3;
		const Response down = int_value(r.frame, r.frame.bounds(), low, Range{.min = 0, .max = 42});
		expect(low).to_equal(0);               // clamps to minValue
		expect(down.changed).to_be_false();

		int32_t unfocused = 41;
		r.input.edit[0] = 1;
		const Response blurred = int_value(r.frame, r.frame.bounds(), unfocused, Range{.min = 0, .max = 42}, {}, false);
		expect(unfocused).to_equal(41);        // edit[] moves only the *focused* value
		expect(blurred.changed).to_be_false();
		expect(blurred.focused).to_be_false();
	});

	it("splits the two edit encoders into a coarse and a fine step", _ {
		// Decimal::getNumberEditSize is legacy's only two-granularity number edit: a coarse step
		// and a 1-unit fine one. edit[0] takes range.step, edit[1] takes 1.
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

		// Legacy Decimal draws at a literal y of 20 -- canvas rows, with no OLED_MAIN_TOPMOST_PIXEL
		// term anywhere in the routine. 18*4 + 9 = 81 wide, so (128 - 81) >> 1 = 23: "12" at
		// 23..58, "." at 59..67, "34" at 68..103.
		for (int x = 0; x < 128; x++) {
			expect(r.lit(x, 19)).to_be_false();
			expect(r.lit(x, 40)).to_be_false();
		}
		expect(r.lit(33, 20)).to_be_true();    // top of the '1'
		expect(r.lit(98, 20)).to_be_true();    // top of the '4'
		expect(r.lit(63, 34)).to_be_true();    // the period, half a digit wide at 59..67
		expect(r.lit(63, 20)).to_be_false();

		// setupBlink is armed but has not fired: shouldBlinkImmediately is false on a plain
		// redraw, so the band stays dark for one kFlashTime.
		for (int x = 0; x < 128; x++) {
			expect(r.lit(x, 41)).to_be_false();
			expect(r.lit(x, 42)).to_be_false();
		}
		expect(r.gate.next_deadline().has_value()).to_be_true();
		expect(*r.gate.next_deadline()).to_equal(uint32_t(kFlashTime));

		// One half-period later performBlink inverts the band: x = ourDigitStartX + 1 = 87,
		// width digitWidth - 2 = 16, rows 41..42 inclusive. The shell wakes on the deadline and
		// consumes it first -- RepaintGate keeps the *earliest* request until needs_frame()
		// clears it, so without this the deadline below would still read 110.
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
		// places 0 skips the whole period branch: 18*2 = 36 wide, at 46.
		whole.reference.draw_string("42", 46, 20, 18, 20, 0, 128, true);
		expect(whole.pbm()).to_equal(whole.reference_pbm());
	});

	it("moves the cursor with the horizontal encoder and lights it at once", _ {
		uitest::Rig48 r;
		int32_t value = 1234;
		int8_t edit_pos = 0;
		Blink blink;
		r.input.now_ms = 5000;
		r.input.horizontal = -1;               // one place left
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(int(edit_pos)).to_equal(1);
		// movingCursor -> setupBlink(..., true) inverts immediately: the cursor is one digit
		// (18px) left of where edit_pos 0 put it, i.e. 69..84.
		expect(r.lit(69, 41)).to_be_true();
		expect(r.lit(84, 42)).to_be_true();
		expect(r.lit(85, 41)).to_be_false();
		expect(r.lit(87, 41)).to_be_false();
		expect(*r.gate.next_deadline()).to_equal(uint32_t(5000 + kFlashTime));

		r.input.horizontal = 1;                // back to the right
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(int(edit_pos)).to_equal(0);
		expect(r.lit(87, 41)).to_be_true();

		// At the right-hand end a further positive tick does nothing.
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(int(edit_pos)).to_equal(0);
	});

	it("restarts the blink lit on a horizontal tick that moves nothing", _ {
		// Legacy sets movingCursor after and outside the branch that moves the cursor, so a
		// blocked tick still reaches setupBlink(..., true) and inverts the band at once.
		// Reachable in the default state: getDefaultEditPos is 0, so a rightward tick there
		// moves nothing.
		uitest::Rig48 r;
		int32_t value = 1234;
		int8_t edit_pos = 0;
		Blink blink;
		r.input.now_ms = 5000;
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(r.lit(87, 41)).to_be_false();   // plain redraw: dark for the first kFlashTime

		r.input.horizontal = 1;                // blocked: edit_pos is already 0
		decimal_value(r.frame, r.frame.bounds(), value, Range{.min = 0, .max = 9999}, 2, edit_pos, blink);
		expect(int(edit_pos)).to_equal(0);
		expect(r.lit(87, 41)).to_be_true();    // ... but the cursor lights anyway
		expect(r.lit(102, 42)).to_be_true();
		expect(r.lit(86, 41)).to_be_false();

		// And the other blocked branch: a negative tick when the next place up leaves the range
		// (numberEditSize * 10 <= max; 10^2 * 10 > 99).
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
		expect(value).to_equal(1334);          // offset * getNumberEditSize()
		expect(resp.changed).to_be_true();

		r.input.edit[0] = 100;                 // way past the top: clamps
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
		// Selection's window rule over kOLEDMenuNumOptionsVisible = 3: nBefore 3, nAfter 2, pos 1
		// -> first visible name is index 3.
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
		r.reference.invert_area_rounded(0, 128, 28, 36, BorderRadius::Small, true);
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
		r64.reference.invert_area_rounded(0, 128, 15 + 27, 15 + 27 + 8, BorderRadius::Small, true);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
	});

	it("clamps by default and wraps when asked", _ {
		const std::string_view names[] = {"AAA", "BBB", "CCC", "DDD", "EEE", "FFF"};
		uitest::Rig48 r;
		ListState st;
		ScrollText sc;

		// Enumeration::wrapAround() is false by default, so the out-of-range turn clamps.
		int32_t index = 5;
		r.input.turn = 1;
		const Response clamped = enum_value(r.frame, menu_rect(r.frame), index, names, st, sc);
		expect(index).to_equal(5);
		expect(clamped.changed).to_be_false();

		// mod(nextValue, numOptions), which is positive-valued.
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
		r.reference.invert_area_rounded(0, 128, 28, 36, BorderRadius::Small, true);
		expect(r.pbm()).to_equal(r.reference_pbm());

		// Both ends, where Selection's pos/tail rule switches branches: at the head nBefore < pos
		// pulls pos to 0, at the tail nAfter < tail pulls the window flush against the end.
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
		tail.reference.invert_area_rounded(0, 128, 37, 45, BorderRadius::Small, true);
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
		// x follows the rect -- (64 - 36) >> 1 = 14 -- but y is still the canvas-absolute 20,
		// not rect.y + anything.
		d.reference.draw_string("42", 14, 20, 18, 20, 0, 64, true);
		expect(d.pbm()).to_equal(d.reference_pbm());

		uitest::Rig48 t;
		bool on = true;
		ScrollText sc;
		toggle(t.frame, Rect{0, 20, 128, 18}, on, "OFF", "ON", sc);
		// Rows at local 20 and 29, i.e. canvas 25 and 34.
		t.reference.draw_string("OFF", kTextSpacingX, 25, kTextSpacingX, kTextSpacingY);
		t.reference.draw_string("ON", kTextSpacingX, 34, kTextSpacingX, kTextSpacingY);
		t.reference.invert_area_rounded(0, 128, 34, 42, BorderRadius::Small, true);
		expect(t.pbm()).to_equal(t.reference_pbm());

		const std::string_view names[] = {"AAA", "BBB"};
		uitest::Rig48 e;
		int32_t index = 1;
		ListState st;
		ScrollText esc;
		enum_value(e.frame, Rect{0, 20, 128, 18}, index, names, st, esc);
		e.reference.draw_string("AAA", kTextSpacingX, 25, kTextSpacingX, kTextSpacingY);
		e.reference.draw_string("BBB", kTextSpacingX, 34, kTextSpacingX, kTextSpacingY);
		e.reference.invert_area_rounded(0, 128, 34, 42, BorderRadius::Small, true);
		expect(e.pbm()).to_equal(e.reference_pbm());
	});

	// ------------------------------------------------------------------- toggle

	it("draws the two legacy toggle rows with the current one highlighted", _ {
		uitest::Rig48 r;
		bool value = true;
		ScrollText sc;
		const Response resp = toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc);
		expect(resp.changed).to_be_false();

		// yPixel = 14 + OLED_MAIN_TOPMOST_PIXEL = 19, rows kTextSpacingY apart, drawString at
		// kTextSpacingX, and invertLeftEdgeForMenuHighlighting(0, 128, yPixel, yPixel + 8) on the
		// selected one.
		expect(r.lit(6, 21)).to_be_true();     // the 'O' of the unhighlighted first row
		expect(r.lit(0, 28)).to_be_false();    // rounded corner of the second row's highlight
		expect(r.lit(0, 29)).to_be_true();
		expect(r.lit(127, 36)).to_be_false();  // bottom-right corner
		expect(r.lit(126, 36)).to_be_true();
		r.reference.draw_string("OFF", kTextSpacingX, 19, kTextSpacingX, kTextSpacingY);
		r.reference.draw_string("ON", kTextSpacingX, 28, kTextSpacingX, kTextSpacingY);
		r.reference.invert_area_rounded(0, 128, 28, 36, BorderRadius::Small, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/toggle_on_48.pbm", r.pbm());

		uitest::Rig48 off;
		bool off_value = false;
		ScrollText off_sc;
		toggle(off.frame, menu_rect(off.frame), off_value, "OFF", "ON", off_sc);
		off.reference.draw_string("OFF", kTextSpacingX, 19, kTextSpacingX, kTextSpacingY);
		off.reference.draw_string("ON", kTextSpacingX, 28, kTextSpacingX, kTextSpacingY);
		off.reference.invert_area_rounded(0, 128, 19, 27, BorderRadius::Small, true);
		expect(off.pbm()).to_equal(off.reference_pbm());

		// The 64-row panel puts the same two rows at base-y 15.
		uitest::Rig64 r64;
		bool v64 = true;
		ScrollText sc64;
		toggle(r64.frame, menu_rect(r64.frame), v64, "OFF", "ON", sc64);
		r64.reference.draw_string("OFF", kTextSpacingX, 15, kTextSpacingX, kTextSpacingY);
		r64.reference.draw_string("ON", kTextSpacingX, 24, kTextSpacingX, kTextSpacingY);
		r64.reference.invert_area_rounded(0, 128, 24, 32, BorderRadius::Small, true);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
	});

	it("selects by turn direction and leaves the value alone on a select press", _ {
		uitest::Rig48 r;
		bool value = false;
		ScrollText sc;

		// Toggle::selectEncoderAction is setValue(offset > 0), NOT a flip: turning up twice
		// leaves it on.
		r.input.turn = 1;
		expect(toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc).changed).to_be_true();
		expect(value).to_be_true();
		expect(toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc).changed).to_be_false();
		expect(value).to_be_true();

		r.input.turn = -1;
		expect(toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc).changed).to_be_true();
		expect(value).to_be_false();

		// A select press on THIS screen does not touch the value. The two-row screen only exists
		// while the Toggle is the current menu item -- Toggle::shouldEnterSubmenu() is false, so
		// no submenu navigation can enter it -- and on that branch Toggle::selectButtonPress
		// returns nullptr without calling toggleValue(), after which the scene runs
		// goUpOneLevel(). The toggleValue() branch fires only for a Toggle drawn as a row inside a
		// Submenu, where the parent's list row carries the checkbox icon.
		r.input.turn = 0;
		r.input.select_pressed = true;
		const bool before = value;
		const Response pressed = toggle(r.frame, menu_rect(r.frame), value, "OFF", "ON", sc);
		expect(value).to_equal(before);
		expect(pressed.changed).to_be_false();
		expect(pressed.entered).to_be_true();   // the scene navigates, as it does for enum_value
	});

	// ---------------------------------------------------------------- range_value

	// `Range::drawPixelsForOled` (`gui/menu_item/range.cpp:97-131`) is a *different* legacy routine
	// from `Integer::drawPixelsForOled`, and the difference is load-bearing: it centres on
	// `kTextHugeSpacingX * character_count` -- a monospaced width -- where Integer centres on
	// `get_string_width_in_pixels`. For "60" the two agree by luck; for the strings below they do
	// not, which is why each example replays legacy's own `draw_string(buffer, stringStartX, ...)`
	// rather than `draw_string_centred`.

	it("draws just the low end when the ends are equal and no edge is selected", _ {
		uitest::Rig48 r;
		int32_t lower = 120, upper = 120;
		RangeEdge edge = RangeEdge::Off;
		const Response resp = range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240}, edge,
		                                  fmt_int, RangeClamp::Push);
		expect(resp.focused).to_be_true();
		expect(resp.changed).to_be_false();

		// getText's mayShowJustOne arm (`integer_range.cpp:79-84`): rightLength 0, so textLength
		// is 3, stringWidth 54, stringStartX (128 - 54) >> 1 == 37.
		r.reference.draw_string("120", 37, 18 + 5, kTextHugeSpacingX, kTextHugeSizeY);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/range_value_point_48.pbm", r.pbm());

		uitest::Rig64 r64;
		int32_t l64 = 120, u64 = 120;
		RangeEdge e64 = RangeEdge::Off;
		range_value(r64.frame, r64.frame.bounds(), l64, u64, Range{.min = 60, .max = 240}, e64, fmt_int,
		            RangeClamp::Push);
		r64.reference.draw_string("120", 37, 18 + 0, kTextHugeSpacingX, kTextHugeSizeY);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
		expect_golden(self, "../ui/widgets/range_value_point_64.pbm", r64.pbm());
	});

	it("draws both ends with a hyphen once they differ, at both geometries", _ {
		uitest::Rig48 r;
		int32_t lower = 90, upper = 150;
		RangeEdge edge = RangeEdge::Off;
		range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240}, edge, fmt_int,
		            RangeClamp::Push);
		// leftLength 2 + rightLength 3 + 1 for the '-' == 6; stringWidth 108; startX (128-108)>>1 == 10.
		r.reference.draw_string("90-150", 10, 18 + 5, kTextHugeSpacingX, kTextHugeSizeY);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/range_value_split_48.pbm", r.pbm());

		uitest::Rig64 r64;
		int32_t l64 = 90, u64 = 150;
		RangeEdge e64 = RangeEdge::Off;
		range_value(r64.frame, r64.frame.bounds(), l64, u64, Range{.min = 60, .max = 240}, e64, fmt_int,
		            RangeClamp::Push);
		r64.reference.draw_string("90-150", 10, 18 + 0, kTextHugeSpacingX, kTextHugeSizeY);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
		expect_golden(self, "../ui/widgets/range_value_split_64.pbm", r64.pbm());
	});

	it("shows both ends even when they are equal, once an edge is selected", _ {
		// `mayShowJustOne` is `editingRangeEdge == RangeEdit::OFF` (`range.cpp:102`), so selecting an
		// edge on a coincident pair reveals the hyphen -- which is how the user sees there are two.
		uitest::Rig48 r;
		int32_t lower = 7, upper = 7;
		RangeEdge edge = RangeEdge::Left;
		range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 0, .max = 99}, edge, fmt_int,
		            RangeClamp::Push);
		// "7-7": textLength 3, stringWidth 54, startX 37. Left highlight is 1 digit wide from 37.
		r.reference.draw_string("7-7", 37, 18 + 5, kTextHugeSpacingX, kTextHugeSizeY);
		r.reference.invert_area(37, kTextHugeSpacingX, 18 + 5 - 1, 18 + 5 - 1 + kTextHugeSizeY + 1);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("inverts the selected end plus one row above and below, at both geometries", _ {
		// `range.cpp:117-130`: LEFT inverts from stringStartX for leftLength digits; RIGHT inverts
		// backwards from stringEndX for rightLength. The band is `baseY .. baseY + digitHeight + 1`
		// with `baseY` already decremented by one, i.e. digitHeight + 2 rows.
		uitest::Rig48 left;
		int32_t l = 90, u = 150;
		RangeEdge edge = RangeEdge::Left;
		range_value(left.frame, left.frame.bounds(), l, u, Range{.min = 60, .max = 240}, edge, fmt_int,
		            RangeClamp::Push);
		left.reference.draw_string("90-150", 10, 18 + 5, kTextHugeSpacingX, kTextHugeSizeY);
		left.reference.invert_area(10, kTextHugeSpacingX * 2, 18 + 5 - 1, 18 + 5 - 1 + kTextHugeSizeY + 1);
		expect(left.pbm()).to_equal(left.reference_pbm());
		expect_golden(self, "../ui/widgets/range_value_left_48.pbm", left.pbm());

		uitest::Rig48 right;
		int32_t l2 = 90, u2 = 150;
		RangeEdge redge = RangeEdge::Right;
		range_value(right.frame, right.frame.bounds(), l2, u2, Range{.min = 60, .max = 240}, redge, fmt_int,
		            RangeClamp::Push);
		right.reference.draw_string("90-150", 10, 18 + 5, kTextHugeSpacingX, kTextHugeSizeY);
		// stringEndX = (128 + 108) >> 1 == 118; the 3-digit right end starts at 118 - 54 == 64.
		right.reference.invert_area(64, kTextHugeSpacingX * 3, 18 + 5 - 1, 18 + 5 - 1 + kTextHugeSizeY + 1);
		expect(right.pbm()).to_equal(right.reference_pbm());
		expect_golden(self, "../ui/widgets/range_value_right_48.pbm", right.pbm());

		uitest::Rig64 r64;
		int32_t l64 = 90, u64 = 150;
		RangeEdge e64 = RangeEdge::Right;
		range_value(r64.frame, r64.frame.bounds(), l64, u64, Range{.min = 60, .max = 240}, e64, fmt_int,
		            RangeClamp::Push);
		r64.reference.draw_string("90-150", 10, 18 + 0, kTextHugeSpacingX, kTextHugeSizeY);
		r64.reference.invert_area(64, kTextHugeSpacingX * 3, 18 + 0 - 1, 18 + 0 - 1 + kTextHugeSizeY + 1);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
		expect_golden(self, "../ui/widgets/range_value_right_64.pbm", r64.pbm());
	});

	it("walks the edge with the horizontal encoder exactly as Range::horizontalEncoderAction does", _ {
		// `range.cpp:35-84` as a table: a turn towards the end already selected switches editing
		// off; any other turn selects the end the turn points at.
		struct Step {
			RangeEdge from;
			int8_t horizontal;
			RangeEdge to;
		};
		const Step steps[] = {
		    {RangeEdge::Off, -1, RangeEdge::Left},    // `:52-53`
		    {RangeEdge::Off, 1, RangeEdge::Right},    // `:73-74`
		    {RangeEdge::Left, -1, RangeEdge::Off},    // `:44-46`
		    {RangeEdge::Left, 1, RangeEdge::Right},   // `:73-74` from LEFT
		    {RangeEdge::Right, 1, RangeEdge::Off},    // `:67-68`
		    {RangeEdge::Right, -1, RangeEdge::Left},  // `:52-53` from RIGHT
		    {RangeEdge::Left, 0, RangeEdge::Left},    // no turn, no move
		};
		for (const Step& step : steps) {
			uitest::Rig48 r;
			int32_t lower = 90, upper = 150;
			RangeEdge edge = step.from;
			r.input.horizontal = step.horizontal;
			const Response resp = range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240},
			                                  edge, fmt_int, RangeClamp::Push);
			expect(static_cast<int>(edge)).to_equal(static_cast<int>(step.to));
			expect(resp.changed).to_equal(step.from != step.to);
			expect(lower).to_equal(90);   // the horizontal encoder never moves a value
			expect(upper).to_equal(150);
		}
	});

	it("reads nothing at all when unfocused", _ {
		uitest::Rig48 r;
		int32_t lower = 90, upper = 150;
		RangeEdge edge = RangeEdge::Left;
		r.input.horizontal = 1;
		r.input.edit[0] = 3;
		const Response resp = range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240}, edge,
		                                  fmt_int, RangeClamp::Push, /*focused=*/false);
		expect(resp.focused).to_be_false();
		expect(static_cast<int>(edge)).to_equal(static_cast<int>(RangeEdge::Left));
		expect(lower).to_equal(90);
		expect(upper).to_equal(150);
		// ... and it still draws, which is what an unfocused render is for.
		r.reference.draw_string("90-150", 10, 18 + 5, kTextHugeSpacingX, kTextHugeSizeY);
		r.reference.invert_area(10, kTextHugeSpacingX * 2, 18 + 5 - 1, 18 + 5 - 1 + kTextHugeSizeY + 1);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("pushes the far end with RangeClamp::Push, the IntegerRange rule", _ {
		// `IntegerRange::selectEncoderAction` (`integer_range.cpp:39-52`): each end moves inside
		// [min, max] and drags the other along rather than crossing it.
		{
			uitest::Rig48 r;
			int32_t lower = 100, upper = 100;
			RangeEdge edge = RangeEdge::Left;
			r.input.edit[0] = 5;
			expect(range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240}, edge, fmt_int,
			                   RangeClamp::Push)
			           .changed)
			    .to_be_true();
			expect(lower).to_equal(105);
			expect(upper).to_equal(105);   // dragged
		}
		{
			uitest::Rig48 r;
			int32_t lower = 100, upper = 100;
			RangeEdge edge = RangeEdge::Right;
			r.input.edit[0] = -5;
			range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240}, edge, fmt_int,
			            RangeClamp::Push);
			expect(upper).to_equal(95);
			expect(lower).to_equal(95);    // dragged the other way
		}
		{
			uitest::Rig48 r;   // and it still clamps at the outer bounds
			int32_t lower = 60, upper = 240;
			RangeEdge edge = RangeEdge::Left;
			r.input.edit[0] = -1;
			expect(range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240}, edge, fmt_int,
			                   RangeClamp::Push)
			           .changed)
			    .to_be_false();
			expect(lower).to_equal(60);
		}
	});

	it("bounds each end against the other with RangeClamp::Bound, the KeyRange rule", _ {
		// `KeyRange::selectEncoderAction` (`key_range.cpp:36-43`): the other end IS the bound, so
		// an end stops dead at its neighbour and the neighbour never moves. This is the example that
		// separates the two policies: with Push the same detent would give 6-6.
		{
			uitest::Rig48 r;
			int32_t lower = 5, upper = 5;
			RangeEdge edge = RangeEdge::Left;
			r.input.edit[0] = 1;
			expect(range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 0, .max = 11}, edge, fmt_int,
			                   RangeClamp::Bound)
			           .changed)
			    .to_be_false();
			expect(lower).to_equal(5);
			expect(upper).to_equal(5);
		}
		{
			uitest::Rig48 r;
			int32_t lower = 2, upper = 9;
			RangeEdge edge = RangeEdge::Left;
			r.input.edit[0] = 100;   // way past the upper end
			range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 0, .max = 11}, edge, fmt_int,
			            RangeClamp::Bound);
			expect(lower).to_equal(9);
			expect(upper).to_equal(9);   // untouched: it was already there
		}
		{
			uitest::Rig48 r;
			int32_t lower = 2, upper = 9;
			RangeEdge edge = RangeEdge::Right;
			r.input.edit[0] = -100;
			range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 0, .max = 11}, edge, fmt_int,
			            RangeClamp::Bound);
			expect(upper).to_equal(2);
			expect(lower).to_equal(2);
		}
	});

	it("with no edge selected, moves a coincident pair as one and refuses to move a split one", _ {
		// The one statement both subclasses share (`integer_range.cpp:57-67`,
		// `key_range.cpp:48-57`), and the reason it matters: a range the user has not opened an
		// edge on behaves exactly like a plain number until it is split.
		{
			uitest::Rig48 r;
			int32_t lower = 100, upper = 100;
			RangeEdge edge = RangeEdge::Off;
			r.input.edit[1] = 3;   // the fine knob, one unit per detent
			expect(range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240}, edge, fmt_int,
			                   RangeClamp::Push)
			           .changed)
			    .to_be_true();
			expect(lower).to_equal(103);
			expect(upper).to_equal(103);
		}
		{
			uitest::Rig48 r;
			int32_t lower = 90, upper = 150;
			RangeEdge edge = RangeEdge::Off;
			r.input.edit[0] = 5;
			expect(range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240}, edge, fmt_int,
			                   RangeClamp::Push)
			           .changed)
			    .to_be_false();
			expect(lower).to_equal(90);
			expect(upper).to_equal(150);
		}
	});

	it("moves nothing across twenty quiet ticks", _ {
		// Nothing here is clock-driven -- there is no scroller and no blink -- so the panel must be
		// byte-identical after twenty input-free ticks, and no repaint may be requested.
		uitest::Rig48 r;
		int32_t lower = 90, upper = 150;
		RangeEdge edge = RangeEdge::Right;
		range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240}, edge, fmt_int,
		            RangeClamp::Push);
		const std::string first = r.pbm();
		for (uint32_t t = 1; t <= 20; t++) {
			r.begin_tick(t * 15);
			const Response resp = range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 60, .max = 240},
			                                  edge, fmt_int, RangeClamp::Push);
			expect(resp.changed).to_be_false();
			expect(r.pbm()).to_equal(first);
		}
		expect(lower).to_equal(90);
		expect(upper).to_equal(150);
		expect(static_cast<int>(edge)).to_equal(static_cast<int>(RangeEdge::Right));
	});

	it("spells each end with whatever the caller's formatter says", _ {
		// The `RangeFormatFn` seam: `KeyRange::getText` spells note letters, not digits, and the
		// widget must lay out on the *formatted* lengths -- a two-character end ("C#") is two digit
		// cells wide even though nothing about the value says so.
		uitest::Rig48 r;
		int32_t lower = 1, upper = 4;   // C# and E, in the sharps table
		RangeEdge edge = RangeEdge::Off;
		range_value(r.frame, r.frame.bounds(), lower, upper, Range{.min = 0, .max = 11}, edge, fmt_note,
		            RangeClamp::Bound);
		// "C#-E": textLength 4, stringWidth 72, startX (128 - 72) >> 1 == 28.
		r.reference.draw_string("C#-E", 28, 18 + 5, kTextHugeSpacingX, kTextHugeSizeY);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/range_value_notes_48.pbm", r.pbm());
	});
});
DELUGE_SPEC(ui_values_spec, values_w)
