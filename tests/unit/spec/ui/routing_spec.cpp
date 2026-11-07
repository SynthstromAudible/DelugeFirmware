#include "../gfx/support/golden.h"
#include "gfx/oled/canvas.h"
#include "gfx/oled/font.h"
#include "gfx/oled/icons.h"
#include "support/frames.h"
#include "ui/core/blink.h"
#include "ui/core/metrics.h"
#include "ui/core/style.h"
#include "ui/oled/routing.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::Canvas;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

// `routing_chain` / `segmented_switch` / `cable_strength_value` -- the three blocks of legacy
// `PatchCableStrength::renderOLED` (`gui/menu_item/patch_cable_strength.cpp:80-195`): the first two draw a
// *routing*, the third its strength. Pinned the three ways this directory pins a widget:
//
// First, byte for byte against the legacy routine: `legacy_diagram` / `legacy_switch` below are
// those two blocks transcribed against the raw gfx `Canvas` in canvas-absolute coordinates, with
// every magic number spelled as a literal (2 for the left padding, 1 for `extraY`, 8 vs 9 for the
// two vertical pitches, 4/16 for the elbow, 73/25/28/34 for the switch, 6/7/9 for the Apple ][
// font) rather than borrowed from the constants the port lays out with.
//
// Second, as individual coordinates read off the legacy source: where each elbow segment starts and
// stops, where the bracket's corner pixels land, and where a slot's label sits relative to its own
// inversion.
//
// Third, as golden PBMs at both shipped geometries.
//
// Points worth calling out explicitly:
//
//  - **The two chain shapes use different pitches.** Two stages step by `kTextSpacingY` (9) and
//    start at `extraY + 4`; three stages step by a literal 8 and start at `extraY + 1`
//    (`:86-88`). They are not the same diagram with a row added.
//  - **In the three-stage form the elbow lands *below* the middle stage's label** -- the drop from
//    stage 0 runs to `y + (ySpacing << 1)` while the label is drawn at `y - 3` after a
//    `+= ySpacing - 1` (`:96-97`, `:100-104`). It looks like a bug and it is legacy's; transcribing it
//    is the point of having an oracle rather than a hand-written expectation.
//  - **A slot's label box and its inversion box differ by one column** for every slot but the
//    first (`:144` vs `:147-149`) -- the same class of legacy asymmetry `option_window` reproduces
//    between its label and its highlight.
//  - **Neither widget uses a 48/64 constant pair**, unlike `midi_command`'s row: every legacy
//    coordinate in these two blocks is either an offset from the left edge or
//    `OLED_MAIN_TOPMOST_PIXEL + n`, and a frame's origin already carries `visible_top`. So the same
//    rect gives the same *frame-local* pixels on both panels, which is what the two golden pairs
//    below show. (Legacy's switcher does hard-code an absolute canvas row 34; that is a *placement*
//    choice and belongs to the caller, which is why `area` is a parameter.)

namespace {

/// @brief `PatchCableStrength::renderOLED`'s first block (`patch_cable_strength.cpp:79-123`),
///        transcribed.
///
/// @param c        Canvas to draw into, in canvas-absolute coordinates.
/// @param topmost  `OLED_MAIN_TOPMOST_PIXEL`.
/// @param stages   The path; two entries take the `isJustAParam` arm, three the other.
void legacy_diagram(Canvas& c, int32_t topmost, std::span<const std::string_view> stages) {
	const int32_t extraY = 1;
	const int32_t leftPadding = 2;
	const bool isJustAParam = stages.size() == 2;
	const int32_t ySpacing = isJustAParam ? 9 : 8; // kTextSpacingY : 8
	int32_t y = extraY + topmost;
	y += isJustAParam ? 4 : 1;

	c.draw_string(stages[0], leftPadding, y, 6, 7); // kTextSpacingX, kTextSizeYUpdated
	y += ySpacing;

	if (!isJustAParam) {
		const int32_t horizontalLineY = y + (ySpacing << 1);
		c.draw_vertical_line(leftPadding + 4, y + 1, horizontalLineY);
		c.draw_horizontal_line(horizontalLineY, leftPadding + 4, leftPadding + 6 * 2 + 4);
		c.draw_graphic_multi_line(deluge::gfx::oled::icons::arrowRight.data, leftPadding + 3 + 6, horizontalLineY - 2,
		                          3);
		y += ySpacing - 1;

		c.draw_string(stages[1], leftPadding + 6 * 2, y - 3, 6, 7);
		y += ySpacing;

		c.draw_vertical_line(leftPadding + 6 * 2 + 4, y - 2, y + 2);
		c.draw_graphic_multi_line(deluge::gfx::oled::icons::arrowDown.data, leftPadding + 6 * 2 + 2, y, 5);
		y += ySpacing;
	}
	else {
		c.draw_vertical_line(leftPadding + 3, y + 2, y + 5);
		c.draw_graphic_multi_line(deluge::gfx::oled::icons::arrowDown.data, leftPadding + 1, y + 6, 5);
		y += 16;
	}

	c.draw_string(stages.back(), leftPadding, y, 6, 7);
}

/// @brief `PatchCableStrength::renderOLED`'s polarity switcher (`patch_cable_strength.cpp:125-157`),
///        transcribed.
///
/// @param c        Canvas to draw into, in canvas-absolute coordinates.
/// @param startY   Legacy's own `startY` (34), or wherever the caller puts the band.
/// @param bi_uni   True for the two-slot BI/UNI arm, false for the one-slot "no polarity" arm.
/// @param unipolar Which of the two slots is inverted, on the two-slot arm.
void legacy_switch(Canvas& c, int32_t startY, bool bi_uni, bool unipolar) {
	const int32_t startX = 73;
	const int32_t biSlotWidth = 25;
	const int32_t uniSlotWidth = 28;
	const int32_t endX = startX + biSlotWidth + uniSlotWidth - 1;
	const int32_t endY = startY + 9; // kTextSpacingY

	c.draw_vertical_line(startX - 2, startY, endY);
	c.draw_vertical_line(endX + 2, startY, endY);
	c.draw_horizontal_line(startY - 2, startX, endX);
	c.draw_pixel(startX - 1, startY - 1);
	c.draw_pixel(endX + 1, startY - 1);

	if (bi_uni) {
		c.draw_string_centered("BI", startX, startY, 6, 9, biSlotWidth);
		c.draw_string_centered("UNI", startX + biSlotWidth + 1, startY, 6, 9, uniSlotWidth);
		const int32_t x = unipolar ? startX + biSlotWidth : startX;
		const int32_t width = unipolar ? uniSlotWidth : biSlotWidth;
		c.invert_area_rounded(x, width, startY, endY);
	}
	else {
		c.draw_string_centered("BIPOLAR", startX, startY, 6, 9, biSlotWidth + uniSlotWidth);
	}
}

constexpr std::array<std::string_view, 2> kTwoStage{{"note", "LPF FREQ"}};
constexpr std::array<std::string_view, 3> kThreeStage{{"lfo1", "env2", "LPF FREQ"}};
constexpr std::array<std::string_view, 2> kBiUni{{"BI", "UNI"}};
constexpr std::array<std::string_view, 1> kOneLabel{{"BIPOLAR"}};
constexpr std::array<int32_t, 2> kBiUniWidths{{25, 28}};
constexpr std::array<int32_t, 1> kOneWidth{{53}};

std::span<const std::string_view> two_stage() {
	return {kTwoStage.data(), kTwoStage.size()};
}
std::span<const std::string_view> three_stage() {
	return {kThreeStage.data(), kThreeStage.size()};
}
std::span<const std::string_view> bi_uni() {
	return {kBiUni.data(), kBiUni.size()};
}
std::span<const int32_t> bi_uni_widths() {
	return {kBiUniWidths.data(), kBiUniWidths.size()};
}

/// @brief The frame-local rect that puts a widget at legacy's own canvas origin.
///
/// The diagram's rect is the frame's top-left corner; the frame's origin already carries
/// `visible_top`, so the same `Rect{0, 0, ...}` reproduces legacy's `OLED_MAIN_TOPMOST_PIXEL + n`
/// rows on both panels.
Rect diagram_rect() {
	return Rect{0, 0, 128, 48};
}

/// @brief `PatchCableStrength::renderOLED`'s number block (`patch_cable_strength.cpp:165-195`), transcribed with the
///        division exact (legacy's GCC build truncates it under fast-math: DIFFERENCES.md D71) and the blink drawn lit.
///
/// @param c        Canvas to draw into, in canvas-absolute coordinates.
/// @param topmost  `OLED_MAIN_TOPMOST_PIXEL`.
/// @param value    The strength, in hundredths.
/// @param edit_pos `soundEditor.numberEditPos`.
/// @param lit      Whether the cursor's blink is in its lit phase.
void legacy_number(Canvas& c, int32_t topmost, int32_t value, int32_t edit_pos, bool lit) {
	const int32_t fraction = std::abs(value % 100);
	const int32_t non_zero = fraction == 0 ? 0 : (fraction % 10 == 0 ? 1 : 2);
	const int32_t hidden = std::clamp<int32_t>(2 - non_zero, 0, edit_pos);
	const int32_t shown_pos = edit_pos - hidden;
	const int32_t divisor = hidden == 0 ? 1 : (hidden == 1 ? 10 : 100);
	char buf[12];
	const int32_t shown = value / divisor;
	std::snprintf(buf, sizeof(buf), shown < 0 ? "-%0*d" : "%0*d", static_cast<int>(3 - hidden), std::abs(shown));
	const int32_t textY = 1 + topmost + 4;
	c.draw_string_align_right(buf, textY, 11, 13, 128 - 3); // kTextBigSpacingX, kTextBigSizeY
	if (const int32_t places = 2 - hidden; places > 0) {
		const int32_t separatorX = 128 - places * 11 - 3;
		c.draw_vertical_line(separatorX, textY + 13 + 1, textY + 13 + 3);
		c.draw_vertical_line(separatorX - 1, textY + 13 + 1, textY + 13 + 3);
	}
	if (lit) {
		c.invert_area(128 - (shown_pos + 1) * 11 - 3, 11, textY + 13 + 1, textY + 13 + 1);
	}
}

/// @brief The number's band on the strength page: right of the diagram, from `textY` (frame row 5), 17 rows.
Rect number_rect() {
	return Rect{58, 5, 70, 17};
}

/// @brief Draw @p value at @p edit_pos through the widget, its blink lit, on @p r.
template <class R>
void draw_number(R& r, int32_t value, int8_t edit_pos) {
	Blink blink{};
	blink.period_ms = deluge::ui::kFlashTime;
	blink.restart_lit(0);
	cable_strength_value(r.frame, number_rect(), value, Range{.min = -5000, .max = 5000}, edit_pos, blink);
}

} // namespace

// clang-format off
describe routing_chain_w("ui::oled::routing_chain", $ {

	it("draws legacy's two-stage diagram, byte for byte", _ {
		uitest::Rig48 r;
		routing_chain(r.frame, diagram_rect(), two_stage());
		legacy_diagram(r.reference, 5, two_stage());
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("draws legacy's three-stage diagram, byte for byte", _ {
		uitest::Rig48 r;
		routing_chain(r.frame, diagram_rect(), three_stage());
		legacy_diagram(r.reference, 5, three_stage());
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("is geometry-independent: the same rect draws the same frame-local pixels on a 64-row panel", _ {
		// The claim the header makes, tested rather than asserted: no 48/64 constant pair. The
		// oracle is run at the *other* panel's `OLED_MAIN_TOPMOST_PIXEL` (0), and the port is given
		// the same frame-local rect, so agreement means the widget followed the frame's origin.
		//
		// **Both shapes**, not just the three-stage one: they use different pitches and different
		// starting offsets (`:86-88`), so one of them agreeing at 64 rows says nothing about the
		// other. Fix round 2 -- the two-stage case had been pinned at 64 rows only by a golden.
		{
			uitest::Rig64 r;
			routing_chain(r.frame, diagram_rect(), three_stage());
			legacy_diagram(r.reference, 0, three_stage());
			expect(r.pbm()).to_equal(r.reference_pbm());
		}
		{
			uitest::Rig64 r;
			routing_chain(r.frame, diagram_rect(), two_stage());
			legacy_diagram(r.reference, 0, two_stage());
			expect(r.pbm()).to_equal(r.reference_pbm());
		}
	});

	it("lands each elbow segment on the legacy coordinate", _ {
		uitest::Rig48 r;
		routing_chain(r.frame, diagram_rect(), three_stage());

		// Three-stage: leftPadding 2, ySpacing 8, y starts at canvas `extraY + TOPMOST + 1` = 7, so
		// the elbow's `y` is 15 and `horizontalLineY` is 15 + (8 << 1) = 31.
		//
		//  - the drop, `drawVerticalLine(leftPadding + 4, y + 1, horizontalLineY)`: column 6, canvas
		//    rows 16..31 inclusive, with nothing above or below it. Column 6 is chosen because it is
		//    the one segment no arrowhead overlaps -- the two icons sit at columns 11..13 and 16..20.
		expect(r.lit(6, 15)).to_be_false();
		expect(r.lit(6, 16)).to_be_true();
		expect(r.lit(6, 31)).to_be_true();
		expect(r.lit(6, 32)).to_be_false();
		//  - the horizontal, `drawHorizontalLine(horizontalLineY, leftPadding + 4, leftPadding + 16)`:
		//    row 31, columns 6..18 inclusive and not 19.
		expect(r.lit(18, 31)).to_be_true();
		expect(r.lit(19, 31)).to_be_false();
		//  - `rightArrowIcon` at `(leftPadding + 3 + kTextSpacingX, horizontalLineY - 2)` = (11, 29).
		//    Its third column is a single pixel on the icon's row 2, i.e. canvas (13, 31) -- the tip,
		//    on the horizontal's own row -- and nothing on the row above it.
		expect(r.lit(13, 31)).to_be_true();
		expect(r.lit(13, 30)).to_be_false();
	});

	it("draws nothing for fewer than two stages", _ {
		uitest::Rig48 r;
		const std::array<std::string_view, 1> one{{"note"}};
		routing_chain(r.frame, diagram_rect(), std::span<const std::string_view>{one.data(), one.size()});
		expect(r.pbm()).to_equal(r.reference_pbm()); // reference is still blank
	});

	it("records both shipped geometries", _ {
		{
			uitest::Rig48 r;
			routing_chain(r.frame, diagram_rect(), two_stage());
			expect_golden(self, "../ui/widgets/routing_chain_two_48.pbm", r.pbm());
		}
		{
			uitest::Rig64 r;
			routing_chain(r.frame, diagram_rect(), two_stage());
			expect_golden(self, "../ui/widgets/routing_chain_two_64.pbm", r.pbm());
		}
		{
			uitest::Rig48 r;
			routing_chain(r.frame, diagram_rect(), three_stage());
			expect_golden(self, "../ui/widgets/routing_chain_three_48.pbm", r.pbm());
		}
		{
			uitest::Rig64 r;
			routing_chain(r.frame, diagram_rect(), three_stage());
			expect_golden(self, "../ui/widgets/routing_chain_three_64.pbm", r.pbm());
		}
	});
});

describe segmented_switch_w("ui::oled::segmented_switch", $ {

	it("draws legacy's BI/UNI box, byte for byte, on both slots", _ {
		for (int32_t selected = 0; selected <= 1; selected++) {
			uitest::Rig48 r;
			// Legacy's own `startY = 34` is a canvas-absolute row; frame-local on the 48-row panel
			// is 34 - 5.
			segmented_switch(r.frame, Rect{73, 34 - 5, 53, 9}, bi_uni(), bi_uni_widths(), selected);
			legacy_switch(r.reference, 34, /*bi_uni=*/true, /*unipolar=*/selected == 1);
			expect(r.pbm()).to_equal(r.reference_pbm());
		}
	});

	it("draws legacy's no-polarity arm -- one full-width label, nothing inverted", _ {
		uitest::Rig48 r;
		segmented_switch(r.frame, Rect{73, 34 - 5, 53, 9},
		                 std::span<const std::string_view>{kOneLabel.data(), kOneLabel.size()},
		                 std::span<const int32_t>{kOneWidth.data(), kOneWidth.size()}, -1);
		legacy_switch(r.reference, 34, /*bi_uni=*/false, /*unipolar=*/false);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("puts the bracket's three lines and two corner pixels on the legacy coordinates", _ {
		uitest::Rig48 r;
		segmented_switch(r.frame, Rect{73, 34 - 5, 53, 9}, bi_uni(), bi_uni_widths(), 0);
		// startX 73, endX 73 + 25 + 28 - 1 = 125, startY 34, endY 43 (canvas rows).
		expect(r.lit(71, 34)).to_be_true();  // left vertical, top
		expect(r.lit(71, 43)).to_be_true();  // left vertical, bottom
		expect(r.lit(127, 34)).to_be_true(); // right vertical
		expect(r.lit(127, 43)).to_be_true();
		expect(r.lit(73, 32)).to_be_true();  // horizontal at startY - 2
		expect(r.lit(125, 32)).to_be_true();
		expect(r.lit(72, 32)).to_be_false(); // ... and not one column further out
		expect(r.lit(126, 32)).to_be_false();
		expect(r.lit(72, 33)).to_be_true(); // the two rounding pixels at startY - 1
		expect(r.lit(126, 33)).to_be_true();
		// The bracket is open at the bottom: no line along endY between the two verticals.
		expect(r.lit(100, 44)).to_be_false();
	});

	it("offsets a non-first slot's label one column right of its own inversion, as legacy does", _ {
		// `:144` draws UNI at `startX + biSlotWidth + 1` (98 + 1) while `:147-149` inverts from
		// `startX + biSlotWidth` (98). Shown by rendering the same box twice with only the
		// selection changed and reading the column that the inversion covers but the label does not.
		uitest::Rig48 r0;
		segmented_switch(r0.frame, Rect{73, 34 - 5, 53, 9}, bi_uni(), bi_uni_widths(), 0);
		uitest::Rig48 r1;
		segmented_switch(r1.frame, Rect{73, 34 - 5, 53, 9}, bi_uni(), bi_uni_widths(), 1);
		// Column 98 is the inversion's first column when UNI is selected and is outside it when BI
		// is -- so it flips, while column 97 (still inside BI's slot) flips the other way.
		expect(r0.lit(98, 38) != r1.lit(98, 38)).to_be_true();
		// Both agree with the legacy oracle for their own selection, which is what the byte-for-byte
		// example above already showed; this one names the column that makes the offset visible.
		legacy_switch(r1.reference, 34, true, true);
		expect(r1.pbm()).to_equal(r1.reference_pbm());
	});

	it("is geometry-independent: the same frame-local rect draws the same pixels on a 64-row panel", _ {
		uitest::Rig64 r;
		segmented_switch(r.frame, Rect{73, 34, 53, 9}, bi_uni(), bi_uni_widths(), 1);
		legacy_switch(r.reference, 34, true, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("draws nothing for an empty slot list", _ {
		uitest::Rig48 r;
		segmented_switch(r.frame, Rect{73, 29, 53, 9}, {}, {}, 0);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("records both shipped geometries", _ {
		{
			uitest::Rig48 r;
			segmented_switch(r.frame, Rect{73, 2, 53, 9}, bi_uni(), bi_uni_widths(), 0);
			expect_golden(self, "../ui/widgets/segmented_switch_bi_48.pbm", r.pbm());
		}
		{
			uitest::Rig64 r;
			segmented_switch(r.frame, Rect{73, 2, 53, 9}, bi_uni(), bi_uni_widths(), 1);
			expect_golden(self, "../ui/widgets/segmented_switch_uni_64.pbm", r.pbm());
		}
	});
});

describe cable_strength_value_w("ui::oled::cable_strength_value", $ {

	it("hides a zero fraction while the cursor is left of it, byte for byte", _ {
		uitest::Rig48 r;
		draw_number(r, 2100, 2); // 21.00 on the units: `21`, no tick
		legacy_number(r.reference, 5, 2100, 2, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("marks the fraction with legacy's tick under the digits when the cursor is on it", _ {
		uitest::Rig48 r;
		draw_number(r, 2100, 0); // `2100`, the tick before the last two
		legacy_number(r.reference, 5, 2100, 0, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		// The tick: columns 102-103 (`OLED_MAIN_WIDTH_PIXELS - 2 * digitWidth - rightPadding` and the one left of it), the
		// three rows under the digits' clear row.
		expect(r.lit(102, 26) && r.lit(103, 26) && !r.lit(101, 26) && !r.lit(104, 26)).to_be_true();
		// The cursor under the hundredths, columns 114-124 of row 24.
		expect(r.lit(114, 24) && r.lit(124, 24) && !r.lit(113, 24) && !r.lit(114, 25)).to_be_true();
	});

	it("keeps a non-zero fraction place shown, and a hidden one only right of the cursor", _ {
		uitest::Rig48 a;
		draw_number(a, 2150, 2); // `215`, one place, the tick before the last digit
		legacy_number(a.reference, 5, 2150, 2, true);
		expect(a.pbm()).to_equal(a.reference_pbm());
		uitest::Rig48 b;
		draw_number(b, 0, 1); // `00`: one of the two zero places hidden
		legacy_number(b.reference, 5, 0, 1, true);
		expect(b.pbm()).to_equal(b.reference_pbm());
	});

	it("draws a negative value and a cursor left of every digit as legacy does", _ {
		uitest::Rig48 a;
		draw_number(a, -5000, 2); // `-50`
		legacy_number(a.reference, 5, -5000, 2, true);
		expect(a.pbm()).to_equal(a.reference_pbm());
		uitest::Rig48 b;
		draw_number(b, 0, 3); // `0`, the cursor one place left of it
		legacy_number(b.reference, 5, 0, 3, true);
		expect(b.pbm()).to_equal(b.reference_pbm());
	});

	it("draws its cursor one row tall, on the row under the digits", _ {
		uitest::Rig48 r;
		draw_number(r, 0, 2);
		for (int x = 114; x <= 124; x++) {
			expect(r.lit(x, 24)).to_be_true();
			expect(r.lit(x, 25)).to_be_false();
		}
	});

	it("moves by 10^place a detent, five times that with SHIFT, and not at all when the turn is withheld", _ {
		uitest::Rig48 r;
		int32_t value = 2200;
		int8_t pos = 2;
		Blink blink{};
		r.input.turn = 1;
		r.begin_tick(10);
		Response got = cable_strength_value(r.frame, number_rect(), value, Range{.min = -5000, .max = 5000}, pos, blink);
		expect(got.changed).to_be_true();
		expect(value).to_equal(2300);
		r.input.shift = true;
		r.begin_tick(20);
		(void)cable_strength_value(r.frame, number_rect(), value, Range{.min = -5000, .max = 5000}, pos, blink);
		expect(value).to_equal(2800);
		r.input.shift = false;
		r.begin_tick(30);
		got = cable_strength_value(r.frame, number_rect(), value, Range{.min = -5000, .max = 5000}, pos, blink,
		                           /*take_turn=*/false);
		expect(got.changed).to_be_false();
		expect(value).to_equal(2800);
	});

	it("moves its cursor a place a horizontal tick, up to the highest place under the maximum", _ {
		uitest::Rig48 r;
		int32_t value = 0;
		int8_t pos = 2;
		Blink blink{};
		r.input.horizontal = -1;
		for (uint32_t t = 10; t <= 40; t += 10) {
			r.begin_tick(t);
			(void)cable_strength_value(r.frame, number_rect(), value, Range{.min = -5000, .max = 5000}, pos, blink);
		}
		expect(static_cast<int>(pos)).to_equal(3); // 1000 * 10 > 5000
		r.input.horizontal = 1;
		for (uint32_t t = 50; t <= 100; t += 10) {
			r.begin_tick(t);
			(void)cable_strength_value(r.frame, number_rect(), value, Range{.min = -5000, .max = 5000}, pos, blink);
		}
		expect(static_cast<int>(pos)).to_equal(0);
	});
});
// clang-format on
/// Three `describe` groups in one TU, so `DELUGE_SPEC` (one spec per entry point) is spelled out here
/// with `add_specs` instead -- the shape `sim/scenes_spec/tables_1_spec.cpp` uses for the same
/// reason.
int ui_routing_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(routing_chain_w, segmented_switch_w, cable_strength_value_w)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
