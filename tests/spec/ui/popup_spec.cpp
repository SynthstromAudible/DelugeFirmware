#include "../gfx/support/golden.h"
#include "gfx/oled/canvas.h"
#include "gfx/oled/font.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/core/state.h"
#include "ui/core/style.h"
#include "ui/core/text_wrap.h"
#include "ui/oled/popup.h"

#include "cppspec.hpp"

#include <algorithm>
#include <string>
#include <string_view>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::Canvas;
using deluge::gfx::oled::FontId;
using deluge::gfx::oled::Rect;
using deluge::gfx::oled::Size;
namespace uitest = deluge::ui::test;

// Popups, wrapping, the working animation and the progress bar, pinned three ways, as
// `columns_spec.cpp` and `values_spec.cpp` pin theirs.
//
// First, byte for byte against the legacy routines: `namespace legacy` below re-transcribes
// `addCharacterToLine`, `findLineSplitPoint`, `addLine` and `breakStringIntoLines`
// (hid/display/oled.cpp:398-568), `OLED::setupPopup` (:120-136), `OLED::popupText` (:605-643),
// `OLED::drawPermanentPopupLookingText` (:570-601) and `updateWorkingAnimation` (:647-721)
// against the raw gfx `Canvas` in canvas-absolute coordinates, with every magic number spelled as
// a literal (12 for doubleMargin, 19 for maxCharsPerLine, 6/9 for the Apple ][ font, 5/5/8 for
// the animation's w1/w2/h) rather than borrowed from the constants the port lays out with. The
// wrap oracle is compared field by field; the drawing oracles are replayed onto a second canvas
// and the whole thing compared.
//
// Second, as individual coordinates read off the legacy source, for the checks the task brief
// names: the split at a space, `popup_rect_for`'s box, `Notification` having no border pixel, the
// permanent square rect and `progress`' fill columns.
//
// Third, as golden PBMs, at both shipped geometries.
//
// Deviations, all noted where they occur:
//
//  D1. **The popup is centred on the whole panel, as legacy centres it** -- 48 rows on the
//      shipped board, the five the faceplate hides included (`popupMinY =
//      (OLED_MAIN_HEIGHT_PIXELS - height) / 2`, oled.cpp:128, and `textPixelY`, :620). The port
//      reads `f.canvas.geometry()` and converts by `f.origin`, the same canvas-absolute idiom
//      `segment_digits` and `decimal_value` use, so the oracles below need **no shim at all**:
//      both geometries are compared byte for byte against verbatim legacy handed 128x48 and
//      128x64. An earlier revision of this port centred on `f.bounds()` (43 visible rows), which
//      put every 48-row popup 3 rows low; corrected in fix round 1.
//  D2. `popup_rect_for` returns legacy's `{minX, minY, width, height}` in *frame* coordinates,
//      and its drawn extent is one pixel larger on each axis because `setupPopup` passes
//      `minX + width` as an *inclusive* maximum. The task brief's `{(128-42)/2, (43-21)/2, 42, 21}`
//      for a 30x9 content is superseded on the y term (see D1); the box is canvas `{43, 13, 42, 21}`
//      and the border it produces is 43 columns wide, both asserted below.
//  D3. `working` draws no word and no dots. `displayWorkingAnimation` keeps only
//      `loading = !strcmp(word, "Loading")` (oled.cpp:723); the brief's ""Loading" + animated
//      dots" describes no code in this tree. The port replays the animation's history each frame
//      because an immediate-mode frame has none; the oracle below accumulates it on one canvas
//      exactly as legacy's persistent image did, and the two are compared at every step.
//  D4. `progress` has no legacy of its own -- nothing under src/deluge draws a progress bar. It
//      is `Number::drawHorizontalBar` (gui/menu_item/number.cpp:33-73) with `min_value = 0`,
//      which is the closest legacy pixels there are; the oracle is that routine.
//  D5. `wrap_lines`' `max_chars_per_line` is read by nothing in legacy. Asserted below by wrapping
//      the same string at three different values and getting the same answer.

namespace {

/// Verbatim transcriptions of the legacy routines, drawing through the raw gfx Canvas in
/// canvas-absolute coordinates. The names and the `image.` are legacy's own.
namespace legacy {

constexpr int32_t TEXT_MAX_NUM_LINES = 8;

struct TextLineBreakdown {
	char const* lines[TEXT_MAX_NUM_LINES];
	uint8_t lineLengths[TEXT_MAX_NUM_LINES];
	uint32_t lineWidths[TEXT_MAX_NUM_LINES];
	int32_t numLines;
	int32_t longestLineLength;
	int32_t maxCharsPerLine;
	int32_t longestLineWidth;
	int32_t maxWidthPerLine;
};

/// oled.cpp:404-430.
bool addCharacterToLine(char c, int32_t maxWidthPerLine, int32_t textHeight, int32_t& lineWidth, int32_t& lineLength,
                        int32_t& charSpacing) {
	int32_t charWidth = deluge::gfx::oled::char_width_px(static_cast<uint8_t>(c), textHeight);
	lineWidth += charWidth;
	charSpacing = deluge::gfx::oled::char_spacing_px(static_cast<uint8_t>(c), textHeight, false);
	lineWidth += charSpacing;
	lineLength++;
	return lineWidth <= maxWidthPerLine;
}

/// oled.cpp:441-463.
char const* findLineSplitPoint(char const* wordStart, int32_t maxWidthPerLine, int32_t textHeight, int32_t& lineWidth,
                               int32_t& lineLength, int32_t& charSpacing) {
	char const* space = wordStart;
	while (true) {
		if (*space == '\n' || *space == ' ' || *space == 0 || *space == '_') {
			break;
		}
		if (!addCharacterToLine(*space, maxWidthPerLine, textHeight, lineWidth, lineLength, charSpacing)) {
			return space;
		}
		space++;
	}
	if (*space == '_' || *space == ' ') {
		addCharacterToLine(*space, maxWidthPerLine, textHeight, lineWidth, lineLength, charSpacing);
	}
	return space;
}

/// oled.cpp:465-484.
void addLine(TextLineBreakdown* b, char const* lineStart, int32_t lineWidth, int32_t lineLength) {
	b->lines[b->numLines] = lineStart;
	b->lineLengths[b->numLines] = static_cast<uint8_t>(lineLength);
	if (lineLength > b->longestLineLength) {
		b->longestLineLength = lineLength;
	}
	b->lineWidths[b->numLines] = static_cast<uint32_t>(lineWidth);
	if (lineWidth > b->longestLineWidth) {
		b->longestLineWidth = lineWidth;
	}
	b->numLines++;
}

/// oled.cpp:494-568, `goto` and all.
void breakStringIntoLines(char const* text, TextLineBreakdown* textLineBreakdown, int32_t textHeight) {
	textLineBreakdown->numLines = 0;
	textLineBreakdown->longestLineLength = 0;
	textLineBreakdown->longestLineWidth = 0;

	char const* lineStart = text;
	char const* wordStart = lineStart;

	int32_t lineWidth = 0;
	int32_t lineLength = 0;
	int32_t lineWidthBeforeThisWord = 0;
	int32_t lineLengthBeforeThisWord = 0;
	int32_t charSpacing = 0;

findNextStringSplitPoint:
	lineWidthBeforeThisWord = lineWidth;
	lineLengthBeforeThisWord = lineLength;

	char const* space = findLineSplitPoint(wordStart, textLineBreakdown->maxWidthPerLine, textHeight, lineWidth,
	                                       lineLength, charSpacing);

	if (lineWidth <= textLineBreakdown->maxWidthPerLine) {
		if (*space == '\n' || *space == 0) {
			lineWidth -= charSpacing;
			addLine(textLineBreakdown, lineStart, lineWidth, lineLength);
			if (!*space || textLineBreakdown->numLines == TEXT_MAX_NUM_LINES) {
				return;
			}
			lineStart = space + 1;
			lineWidth = 0;
			lineLength = 0;
			charSpacing = 0;
		}
		wordStart = space + 1;
		goto findNextStringSplitPoint;
	}
	else {
		lineWidthBeforeThisWord -= charSpacing;
		addLine(textLineBreakdown, lineStart, lineWidthBeforeThisWord, lineLengthBeforeThisWord);
		if (textLineBreakdown->numLines == TEXT_MAX_NUM_LINES) {
			return;
		}
		if (*space == ' ') {
			wordStart = lineStart = space + 1;
		}
		else {
			lineStart = wordStart;
		}
		lineWidth = 0;
		lineLength = 0;
		charSpacing = 0;
		goto findNextStringSplitPoint;
	}
}

/// oled.cpp:120-136. `width_pixels`/`height_pixels` stand in for the two macros.
void setupPopup(Canvas& popup, bool notification, int32_t width, int32_t height, int32_t width_pixels,
                int32_t height_pixels, bool rounded) {
	height = std::clamp<int32_t>(height, 0, height_pixels);
	const int32_t popupMinX = (width_pixels - width) / 2;
	const int32_t popupMaxX = popupMinX + width;
	const int32_t popupMinY = (height_pixels - height) / 2;
	const int32_t popupMaxY = popupMinY + height;

	popup.clear_area_exact(popupMinX, popupMinY, popupMaxX, popupMaxY);
	if (!notification) {
		popup.draw_rectangle_rounded(popupMinX, popupMinY, popupMaxX, popupMaxY, deluge::gfx::oled::SMALL, rounded);
	}
}

/// oled.cpp:605-643, minus the two timers. `width_pixels`/`height_pixels` stand in for the two
/// macros and nothing else does: the port reproduces this arithmetic exactly, so the oracle needs
/// no shim of any kind (see D1).
void popupText(Canvas& popup, char const* text, bool notification, int32_t width_pixels, int32_t height_pixels,
               bool rounded) {
	TextLineBreakdown textLineBreakdown{};
	textLineBreakdown.maxCharsPerLine = 19;

	int32_t doubleMargin = 12;
	textLineBreakdown.maxWidthPerLine = width_pixels - doubleMargin;

	breakStringIntoLines(text, &textLineBreakdown, 9); // kTextSpacingY

	int32_t textWidth = textLineBreakdown.longestLineWidth;
	int32_t textHeight = textLineBreakdown.numLines * 9;

	setupPopup(popup, notification, textWidth + doubleMargin, textHeight + doubleMargin, width_pixels, height_pixels,
	           rounded);

	int32_t textPixelY = (height_pixels - textHeight) >> 1;
	if (textPixelY < 0) {
		textPixelY = 0;
	}

	for (int32_t l = 0; l < textLineBreakdown.numLines; l++) {
		if (textPixelY >= height_pixels) {
			continue;
		}
		int32_t textPixelX = (width_pixels - static_cast<int32_t>(textLineBreakdown.lineWidths[l])) >> 1;
		popup.draw_string(std::string_view{textLineBreakdown.lines[l], textLineBreakdown.lineLengths[l]}, textPixelX,
		                  textPixelY, 6, 9); // kTextSpacingX, kTextSpacingY
		textPixelY += 9;
	}
}

/// oled.cpp:570-601, on the same terms.
void drawPermanentPopupLookingText(Canvas& main, char const* text, int32_t width_pixels, int32_t height_pixels) {
	TextLineBreakdown textLineBreakdown{};
	textLineBreakdown.maxCharsPerLine = 19;

	int32_t doubleMargin = 12;
	textLineBreakdown.maxWidthPerLine = width_pixels - doubleMargin;

	breakStringIntoLines(text, &textLineBreakdown, 9);

	int32_t textWidth = textLineBreakdown.longestLineWidth;
	int32_t textHeight = textLineBreakdown.numLines * 9;

	int32_t minX = (width_pixels - textWidth - doubleMargin) >> 1;
	int32_t maxX = width_pixels - minX;

	int32_t minY = (height_pixels - textHeight - doubleMargin) >> 1;
	int32_t maxY = height_pixels - minY;

	// The port clears before it draws (spec B section 6); legacy relied on clearMainImage.
	main.clear_area_exact(minX, minY, maxX, maxY);
	main.draw_rectangle(minX, minY, maxX, maxY);

	int32_t textPixelY = (height_pixels - textHeight) >> 1;
	if (textPixelY < 0) {
		textPixelY = 0;
	}

	for (int32_t l = 0; l < textLineBreakdown.numLines; l++) {
		int32_t textPixelX = (width_pixels - static_cast<int32_t>(textLineBreakdown.lineWidths[l])) >> 1;
		main.draw_string(std::string_view{textLineBreakdown.lines[l], textLineBreakdown.lineLengths[l]}, textPixelX,
		                 textPixelY, 6, 9);
		textPixelY += 9;
	}
}

/// oled.cpp:647-721, minus the timers, accumulating on one canvas exactly as legacy's persistent
/// popup image did. `count` is `working_animation_count`; call it in ascending order from 1.
void updateWorkingAnimation(Canvas& image, bool loading, int32_t count, int32_t width_pixels, int32_t topmost) {
	const int32_t w1 = 5;
	const int32_t w2 = 5;
	const int32_t h = 8;
	int32_t offset = w2 - 2;

	const int32_t animation_width = w1 + w2 * 2 + 2;
	const int32_t animation_height = h + 1;
	const int32_t popupX = width_pixels - 1 - animation_width;
	const int32_t popupY = topmost + 2;

	if (count == 1) { // !started_animation: setupPopup(NOTIFICATION, ...) at popupX/popupY
		image.clear_area_exact(popupX, popupY, popupX + animation_width, popupY + animation_height);
	}

	const int32_t x_max = popupX + animation_width;
	const int32_t x_min = x_max - (w1 + w2 * 2);
	const int32_t x2 = x_max - w2;
	const int32_t y1 = popupY;
	const int32_t y2 = y1 + h - 1;
	int32_t h2 = 0;
	const int32_t x_pos2 = loading ? x2 - count + 1 : x_min + 1 + count;
	const int32_t t_reset = w1 + w2 + (h - 2) * offset;

	if (count == 1) {
		image.clear_area_exact(popupX, popupY, popupX + animation_width - 1, popupY + animation_height - 1);
		image.draw_rectangle(x_min, y1, x_max, y2);
		image.clear_area_exact(x_min + w2 + 1, popupY, x_max - w2 - 1, popupY + animation_height - 1);
		h2 = h - 2;
	}
	else {
		h2 = std::min((count + 2) / offset, h - 2);
	}

	image.clear_area_exact(x_min + 1, y1 + 1, x_max - 1, y1 + h2);
	if (!loading) {
		offset *= -1;
	}
	for (int i = 0; i < h2; i++) {
		int32_t x_pos = x_pos2 + i * offset;
		if (loading && x_pos < x_min) {
			image.draw_horizontal_line(y1 + 1 + i, x2, x_max - 1);
		}
		if (!loading && x_pos > x2) {
			image.draw_horizontal_line(y1 + 1 + i, x_min + 1, x_min + w2);
		}
		x_pos = std::clamp(x_pos, x_min + 1, x2);
		image.draw_horizontal_line(y1 + 1 + i, x_pos, x_pos + w2 - 1);
	}

	if (count == t_reset) {
		if (loading) {
			image.draw_horizontal_line(y2 - 1, x2, x_max - 1);
		}
		else {
			image.draw_horizontal_line(y2 - 1, x_min + 1, x_min + w2);
		}
	}
}

/// `Number::drawHorizontalBar` (gui/menu_item/number.cpp:33-73) with min_value 0.
void drawHorizontalBar(Canvas& canvas, int32_t left_most, int32_t right_most, int32_t y_top, int32_t height,
                       int32_t value, int32_t max_value, bool rounded) {
	if (value == 0) {
		canvas.draw_rectangle_rounded(left_most, y_top, right_most - 1, y_top + height, deluge::gfx::oled::SMALL,
		                              rounded);
		return;
	}
	const uint32_t range = static_cast<uint32_t>(max_value);
	const float pos_fractional = static_cast<float>(value) / range;
	const int32_t width = right_most - left_most;
	const int32_t pos_horizontal = static_cast<int32_t>(pos_fractional * width);

	if (pos_horizontal == 0) {
		canvas.invert_area(left_most, 1, y_top + 1, y_top + height - 1);
	}
	else {
		canvas.invert_area(left_most, pos_horizontal, y_top + 1, y_top + height - 1);
	}
	canvas.draw_rectangle_rounded(left_most, y_top, right_most - 1, y_top + height, deluge::gfx::oled::SMALL, rounded);
}

} // namespace legacy

/// Is the pixel at canvas coordinates \p x, \p y lit on a rig's *reference* canvas? `OledRig`
/// offers `lit` for the port's canvas and `reference_pbm` for the whole oracle image, but not the
/// per-pixel reader the bezel comparison below needs. Kept local rather than added to the shared
/// rig, which tasks 4-8's specs also use.
template <typename Rig>
bool reference_lit(const Rig& r, int32_t x, int32_t y) {
	return (r.reference.bytes()[static_cast<size_t>(y >> 3) * 128u + static_cast<size_t>(x)] & (1u << (y & 7))) != 0;
}

/// The `t_reset` of the shipped animation: w1 + w2 + (h - 2) * (w2 - 2) = 5 + 5 + 6 * 3.
constexpr int32_t kReset = 28;

/// Elapsed ms at which `working` is showing `step` of `cycle`. Mirrors the two setTimer
/// intervals: 350 before step 1 ends, 70 per step through kReset - 1, 350 on kReset.
uint32_t working_time(uint32_t cycle, int32_t step) {
	if (step == 1) {
		return 0;
	}
	const uint32_t cycle_len = static_cast<uint32_t>(kReset - 2) * 70u + 350u;
	const uint32_t into =
	    (step == kReset) ? static_cast<uint32_t>(kReset - 2) * 70u : static_cast<uint32_t>(step - 2) * 70u;
	return 350u + (cycle - 1u) * cycle_len + into;
}

} // namespace

// clang-format off
describe popup_w("ui::oled::popup", $ {
	it("wraps at a space and reports the two lines' widths", _ {
		// The brief's case. Apple ][ at height 9 is monospaced: 6px a character, and a space is
		// width 0 + spacing 6 (gfx/oled/font.cpp:64-113), so "HELLO WORLD THIS " is 17 chars and
		// 102px and "WRAPS" is 5 and 30.
		const std::string_view s = "HELLO WORLD THIS WRAPS";
		const WrappedLines w = wrap_lines(s, 116, kPopupMaxCharsPerLine, FontId::Apple7);

		expect(static_cast<int32_t>(w.count)).to_equal(2);
		expect(std::string(w.lines[0])).to_equal(std::string("HELLO WORLD THIS "));
		expect(std::string(w.lines[1])).to_equal(std::string("WRAPS"));
		expect(w.widths[0]).to_equal(102);
		expect(w.widths[1]).to_equal(30);
		expect(w.longest).to_equal(102);

		// The split really is at the space: line 0 ends with it and line 1 starts after it.
		expect(w.lines[0].back() == ' ').to_be_true();
		expect(w.lines[1].front() == 'W').to_be_true();
		// And both are views into the caller's string, not copies.
		expect(w.lines[0].data() == s.data()).to_be_true();
		expect(w.lines[1].data() == s.data() + 17).to_be_true();
	});

	it("agrees with breakStringIntoLines line for line", _ {
		auto both_ways = [&](const char* text, int32_t max_width) {
			legacy::TextLineBreakdown b{};
			b.maxCharsPerLine = 19;
			b.maxWidthPerLine = max_width;
			legacy::breakStringIntoLines(text, &b, 9);

			const WrappedLines w = wrap_lines(text, max_width, 19, FontId::Apple7);

			expect(static_cast<int32_t>(w.count)).to_equal(b.numLines);
			expect(w.longest).to_equal(b.longestLineWidth);
			for (int32_t i = 0; i < b.numLines; i++) {
				expect(std::string(w.lines[i]))
				    .to_equal(std::string(b.lines[i], b.lineLengths[i]));
				expect(w.widths[i]).to_equal(static_cast<int32_t>(b.lineWidths[i]));
			}
		};

		both_ways("SAVED", 116);
		both_ways("HELLO WORLD THIS WRAPS", 116);
		both_ways("A_VERY_LONG_UNDERSCORED_NAME", 116);
		both_ways("Operation resumed. Save to new file then reboot.", 116);
		both_ways("LINE ONE\nLINE TWO", 116);
		both_ways("SUPERCALIFRAGILISTIC", 116);        // one word longer than the line
		both_ways("", 116);                             // empty
		both_ways("  ", 116);                           // spaces only
		both_ways("TIGHT", 12);                         // narrower than one word
		both_ways("ONE TWO THREE FOUR FIVE SIX SEVEN EIGHT NINE TEN ELEVEN TWELVE", 40); // overflows 8 lines
	});

	it("caps at eight lines and drops the rest, as legacy does", _ {
		// TEXT_MAX_NUM_LINES (oled.cpp:387). The check is *after* addLine (:526, :548), so the
		// count lands on exactly 8 and the tail is silently gone.
		const std::string_view many = "A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK";
		const WrappedLines w = wrap_lines(many, 116, 19, FontId::Apple7);
		expect(static_cast<int32_t>(w.count)).to_equal(kWrapMaxLines);
		expect(std::string(w.lines[7])).to_equal(std::string("H"));

		legacy::TextLineBreakdown b{};
		b.maxCharsPerLine = 19;
		b.maxWidthPerLine = 116;
		legacy::breakStringIntoLines("A\nB\nC\nD\nE\nF\nG\nH\nI\nJ\nK", &b, 9);
		expect(b.numLines).to_equal(kWrapMaxLines);
	});

	it("ignores max_chars_per_line, because legacy never reads the field", _ {
		// D5: `maxCharsPerLine` is set by all three legacy callers and read by none of
		// breakStringIntoLines, findLineSplitPoint, addLine or addCharacterToLine.
		const std::string_view s = "HELLO WORLD THIS WRAPS";
		const WrappedLines a = wrap_lines(s, 116, 1, FontId::Apple7);
		const WrappedLines b = wrap_lines(s, 116, 19, FontId::Apple7);
		const WrappedLines c = wrap_lines(s, 116, 1000, FontId::Apple7);
		expect(static_cast<int32_t>(a.count)).to_equal(static_cast<int32_t>(b.count));
		expect(static_cast<int32_t>(c.count)).to_equal(static_cast<int32_t>(b.count));
		expect(std::string(a.lines[0])).to_equal(std::string(b.lines[0]));
		expect(std::string(c.lines[1])).to_equal(std::string(b.lines[1]));
	});

	it("centres the popup box on the panel, at both geometries", _ {
		// D2. `popupMinX = (OLED_MAIN_WIDTH_PIXELS - width) / 2` and `popupMinY =
		// (OLED_MAIN_HEIGHT_PIXELS - height) / 2` (oled.cpp:126-129), the height being the *whole*
		// 48 rows including the five the faceplate hides. For a 30x9 content that is canvas
		// {43, 13, 42, 21}, i.e. frame-local y 8 once the frame's origin (5) comes off.
		//
		// The task brief's sketch spelled `(43 - 21) / 2 = 11` -- centred on the 43 *visible* rows
		// -- which is 3 rows below where the device puts it. Superseded, per the parity ruling;
		// see popup.h.
		uitest::Rig48 r;
		const Rect box = popup_rect_for(r.frame, Size{30, 9});
		expect(box.x).to_equal((128 - 42) / 2);
		expect(box.y).to_equal((48 - 21) / 2 - 5);
		expect(r.frame.origin.y + box.y).to_equal(13); // the canvas row legacy computes
		expect(box.w).to_equal(30 + kPopupMargin);
		expect(box.h).to_equal(9 + kPopupMargin);

		uitest::Rig64 r64;
		const Rect box64 = popup_rect_for(r64.frame, Size{30, 9});
		expect(box64.x).to_equal((128 - 42) / 2);
		expect(box64.y).to_equal((64 - 21) / 2); // origin 0: the two rules coincide

		// The drawn border is one column and one row larger, because legacy's maxima are
		// inclusive: 43 columns, 43..85, and 22 rows.
		popup_frame(r.frame, box, PopupStyle::Framed);
		const int32_t top = r.frame.origin.y;
		expect(r.lit(box.x, top + box.y + 1)).to_be_true();           // left edge
		expect(r.lit(box.x + box.w, top + box.y + 1)).to_be_true();   // right edge, at x + w
		expect(r.lit(box.x + box.w + 1, top + box.y + 1)).to_be_false();
		expect(r.lit(box.x + 1, top + box.y)).to_be_true();           // top edge
		expect(r.lit(box.x + 1, top + box.y + box.h)).to_be_true();   // bottom edge, at y + h
	});

	it("draws no border at all for a Notification", _ {
		uitest::Rig48 r;
		const Rect box = popup_rect_for(r.frame, Size{30, 9});
		popup_frame(r.frame, box, PopupStyle::Notification);
		const int32_t top = r.frame.origin.y;

		bool any = false;
		for (int32_t x = box.x; x <= box.x + box.w; x++) {
			any = any || r.lit(x, top + box.y) || r.lit(x, top + box.y + box.h);
		}
		for (int32_t y = box.y; y <= box.y + box.h; y++) {
			any = any || r.lit(box.x, top + y) || r.lit(box.x + box.w, top + y);
		}
		expect(any).to_be_false();

		// And the whole canvas is blank: a Notification frame clears and nothing else.
		bool anywhere = false;
		for (int32_t y = 0; y < 48; y++) {
			for (int32_t x = 0; x < 128; x++) {
				anywhere = anywhere || r.lit(x, y);
			}
		}
		expect(anywhere).to_be_false();
	});

	it("draws a square rect for a Permanent popup, and a rounded one otherwise", _ {
		uitest::Rig48 square;
		popup_text(square.frame, "SAVED", PopupStyle::Permanent);
		uitest::Rig48 round;
		popup_text(round.frame, "SAVED", PopupStyle::Framed);

		// drawRectangle inks all four corners (canvas.cpp draw_rectangle); the SMALL rounded one
		// leaves them clear. Both boxes are at the same place for this string, so one corner
		// pixel separates them.
		const Rect box = popup_rect_for(square.frame, Size{30, 9}, PopupStyle::Permanent);
		const int32_t top = square.frame.origin.y;
		expect(square.lit(box.x, top + box.y)).to_be_true();
		expect(round.lit(box.x, top + box.y)).to_be_false();
		expect(square.pbm() == round.pbm()).to_be_false();
	});

	it("matches popupText byte for byte, at both geometries, against verbatim legacy", _ {
		// D1. The oracle is handed the *panel*'s width and height -- 128x48 with the bezel rows
		// included, exactly what OLED_MAIN_WIDTH_PIXELS / OLED_MAIN_HEIGHT_PIXELS are -- and no
		// shim of any kind. The port centres on `f.canvas.geometry()` and converts by `f.origin`,
		// so both geometries compare exactly.
		auto both_ways = [&](auto& r, int32_t panel_h, const char* text, PopupStyle style) {
			popup_text(r.frame, text, style);
			if (style == PopupStyle::Permanent) {
				legacy::drawPermanentPopupLookingText(r.reference, text, 128, panel_h);
			}
			else {
				legacy::popupText(r.reference, text, style == PopupStyle::Notification, 128, panel_h,
				                  r.style.rounded);
			}
			expect(r.pbm()).to_equal(r.reference_pbm());
		};
		auto at_48 = [&](const char* text, PopupStyle style) {
			uitest::Rig48 r;
			both_ways(r, 48, text, style);
		};
		auto at_64 = [&](const char* text, PopupStyle style) {
			uitest::Rig64 r;
			both_ways(r, 64, text, style);
		};

		for (PopupStyle style : {PopupStyle::Framed, PopupStyle::Notification, PopupStyle::Permanent}) {
			at_48("SAVED", style);                       // box canvas rows 13..34
			at_64("SAVED", style);
		}
		at_48("HELLO WORLD THIS WRAPS", PopupStyle::Framed); // two lines, rows 9..39
		at_64("HELLO WORLD THIS WRAPS", PopupStyle::Framed);
		at_64("Operation resumed. Save to new file then reboot.", PopupStyle::Framed);
		at_64("A\nB\nC\nD\nE", PopupStyle::Framed);
		// The 48-row cases stop at two lines on purpose: a three-line box is 39 rows, which
		// panel-centring puts at canvas row 4 -- inside the bezel. See the next example.
	});

	it("drops the border, but not the text, when a popup reaches into the bezel", _ {
		// The one place the port and legacy still differ on a 48-row panel, and it is the frame's
		// clipping contract rather than a placement difference. Now that the box is centred where
		// legacy centres it, a tall popup's top edge lands in the bezel: "Operation resumed..."
		// wraps to three lines, a 39-row box, which legacy puts at canvas row (48 - 39) / 2 = 4 --
		// one row above the visible area.
		//
		// Legacy drew straight onto the canvas and inked row 4 regardless. The port cannot: a
		// frame's clip is the visible area (frame.h / root_frame). And because
		// `OledFrame::rect_rounded` is all-or-nothing against that clip -- a rounded rect has no
		// span decomposition, frame.h -- a box that pokes one row over the top loses its *whole*
		// border, not just that row. **The text still lands, pixel for pixel.**
		//
		// This is the same limitation the "fills the panel" example below pins, but panel-centring
		// brings it within reach of an ordinary three-line popup, so it is worth its own example.
		// The fix is the gfx clip rectangle frame.h already lists as a prerequisite; see the task
		// report's follow-ups.
		const char* long_text = "Operation resumed. Save to new file then reboot.";
		uitest::Rig48 r;
		popup_text(r.frame, long_text);
		legacy::popupText(r.reference, long_text, false, 128, 48, r.style.rounded);

		// Legacy inks the box's top edge at row 4 and its bottom at row 43; the port inks neither.
		expect(reference_lit(r, 60, 4)).to_be_true();
		expect(reference_lit(r, 60, 43)).to_be_true();
		expect(r.lit(60, 4)).to_be_false();
		expect(r.lit(60, 43)).to_be_false();
		// Nor the sides, at columns 1 and 127.
		expect(reference_lit(r, 1, 20)).to_be_true();
		expect(r.lit(1, 20)).to_be_false();

		// The three text lines sit at canvas rows 10, 19 and 28 ((48 - 27) >> 1 = 10, then +9).
		// Across those rows, and inside the border's columns, the two are identical.
		for (int32_t y = 10; y < 37; y++) {
			for (int32_t x = 2; x < 127; x++) {
				expect(r.lit(x, y)).to_equal(reference_lit(r, x, y));
			}
		}
		// And the text really is there, rather than both being blank.
		bool any = false;
		for (int32_t x = 2; x < 127; x++) {
			any = any || r.lit(x, 11);
		}
		expect(any).to_be_true();
	});

	it("renders the popup goldens", _ {
		uitest::Rig48 saved;
		popup_text(saved.frame, "SAVED");
		expect_golden(self, "../ui/widgets/popup_saved_48.pbm", saved.pbm());

		uitest::Rig64 saved64;
		popup_text(saved64.frame, "SAVED");
		expect_golden(self, "../ui/widgets/popup_saved_64.pbm", saved64.pbm());

		uitest::Rig48 wrapped;
		popup_text(wrapped.frame, "HELLO WORLD THIS WRAPS");
		expect_golden(self, "../ui/widgets/popup_wrapped_48.pbm", wrapped.pbm());

		uitest::Rig48 note;
		popup_text(note.frame, "SAVED", PopupStyle::Notification);
		expect_golden(self, "../ui/widgets/popup_notification_48.pbm", note.pbm());

		uitest::Rig48 perm;
		popup_text(perm.frame, "SAVED", PopupStyle::Permanent);
		expect_golden(self, "../ui/widgets/popup_permanent_48.pbm", perm.pbm());
		uitest::Rig64 perm64;
		popup_text(perm64.frame, "SAVED", PopupStyle::Permanent);
		expect_golden(self, "../ui/widgets/popup_permanent_64.pbm", perm64.pbm());
	});

	it("replays the working animation exactly as the persistent canvas accumulated it", _ {
		// D3. The oracle runs legacy's routine over its own canvas from step 1 up to the step the
		// port is being asked for; the port is handed only the elapsed time.
		auto at = [&](bool loading, uint32_t cycle, int32_t step) {
			uitest::Rig48 r;
			// Already armed and anchored at 0, i.e. the animation started then; an unarmed Blink
			// would be re-anchored at `now` and always show step 1.
			Blink blink{.period_ms = static_cast<uint16_t>(kWorkingStepMs), .until_ms = 0};
			r.input = uitest::null_input(working_time(cycle, step));
			working(r.frame, r.frame.bounds(), loading ? "Loading" : "Saving", blink);

			// The oracle's history: step 1, then every step of each cycle up to this one.
			legacy::updateWorkingAnimation(r.reference, loading, 1, 128, 5);
			for (uint32_t c = 1; c <= cycle; c++) {
				const int32_t last = (c == cycle) ? step : kReset;
				for (int32_t s = 2; s <= last; s++) {
					legacy::updateWorkingAnimation(r.reference, loading, s, 128, 5);
				}
			}
			expect(r.pbm()).to_equal(r.reference_pbm());
		};

		for (int32_t s : {1, 2, 3, 5, 10, 17, 27, kReset}) {
			at(true, 1, s);
			at(false, 1, s);
		}
		// And after the reset, where the retained rows come from the cycle-ending frame rather
		// than from the initialiser -- the case an immediate-mode port gets wrong if it only
		// draws the current step.
		for (int32_t s : {2, 3, 4, 7, 15, kReset}) {
			at(true, 2, s);
			at(false, 2, s);
			at(true, 5, s);
		}
	});

	it("arms the blink and asks for the next animation step", _ {
		uitest::Rig48 r;
		Blink blink{};
		r.input = uitest::null_input(1000);
		working(r.frame, r.frame.bounds(), "Loading", blink);

		// period_ms doubles as "armed": kWorkingStepMs, anchored at now.
		expect(static_cast<int32_t>(blink.period_ms)).to_equal(kWorkingStepMs);
		expect(static_cast<int32_t>(blink.until_ms)).to_equal(1000);
		// Step 1 runs for kWorkingPauseMs (oled.cpp:715-716).
		expect(static_cast<int32_t>(r.gate.next_deadline().value())).to_equal(1000 + kWorkingPauseMs);

		// A step in the middle of the run reschedules at kWorkingStepMs (oled.cpp:719). The gate
		// keeps the *earliest* pending deadline, so the shell's own consume comes first.
		uitest::Rig48 r2;
		Blink b2{};
		r2.input = uitest::null_input(1000);
		working(r2.frame, r2.frame.bounds(), "Loading", b2);
		expect(r2.gate.needs_frame(1000 + kWorkingPauseMs, false, false)).to_be_true();
		r2.input = uitest::null_input(1000 + kWorkingPauseMs);
		working(r2.frame, r2.frame.bounds(), "Loading", b2);
		expect(static_cast<int32_t>(r2.gate.next_deadline().value()))
		    .to_equal(1000 + kWorkingPauseMs + kWorkingStepMs);

		// And the last step of a cycle holds for kWorkingPauseMs again (oled.cpp:711-712).
		uitest::Rig48 r3;
		Blink b3{.period_ms = static_cast<uint16_t>(kWorkingStepMs), .until_ms = 0};
		r3.input = uitest::null_input(working_time(1, kReset));
		working(r3.frame, r3.frame.bounds(), "Loading", b3);
		expect(static_cast<int32_t>(r3.gate.next_deadline().value()))
		    .to_equal(static_cast<int32_t>(working_time(1, kReset)) + kWorkingPauseMs);
	});

	it("runs the working animation the other way for Saving", _ {
		uitest::Rig48 load;
		Blink bl{.period_ms = static_cast<uint16_t>(kWorkingStepMs), .until_ms = 0};
		load.input = uitest::null_input(working_time(1, 8));
		working(load.frame, load.frame.bounds(), "Loading", bl);

		uitest::Rig48 save;
		Blink bs{.period_ms = static_cast<uint16_t>(kWorkingStepMs), .until_ms = 0};
		save.input = uitest::null_input(working_time(1, 8));
		working(save.frame, save.frame.bounds(), "Saving", bs);

		expect(load.pbm() == save.pbm()).to_be_false();
		expect_golden(self, "../ui/widgets/working_loading_48.pbm", load.pbm());
		expect_golden(self, "../ui/widgets/working_saving_48.pbm", save.pbm());

		uitest::Rig64 load64;
		Blink b64{.period_ms = static_cast<uint16_t>(kWorkingStepMs), .until_ms = 0};
		load64.input = uitest::null_input(working_time(1, 8));
		working(load64.frame, load64.frame.bounds(), "Loading", b64);
		expect_golden(self, "../ui/widgets/working_loading_64.pbm", load64.pbm());
	});

	it("fills the progress bar by column, and matches drawHorizontalBar", _ {
		// D4. A 100-wide bar at 1/4 fills 25 columns: the fill is `num / den * rect.w`.
		uitest::Rig48 r;
		const Rect bar{14, 20, 100, 11};
		progress(r.frame, bar, 1, 4);
		const int32_t top = r.frame.origin.y;
		const int32_t mid = top + bar.y + 3; // inside the fill band, clear of the outline
		expect(r.lit(bar.x + 24, mid)).to_be_true();
		expect(r.lit(bar.x + 25, mid)).to_be_false();

		legacy::drawHorizontalBar(r.reference, bar.x, bar.right(), top + bar.y, bar.h - 1, 1, 4, r.style.rounded);
		expect(r.pbm()).to_equal(r.reference_pbm());

		auto both_ways = [&](int32_t num, int32_t den) {
			uitest::Rig48 p;
			progress(p.frame, bar, num, den);
			legacy::drawHorizontalBar(p.reference, bar.x, bar.right(), p.frame.origin.y + bar.y, bar.h - 1, num, den,
			                          p.style.rounded);
			expect(p.pbm()).to_equal(p.reference_pbm());
		};
		both_ways(0, 4);   // outline only
		both_ways(4, 4);   // full
		both_ways(1, 400); // too small for a column: legacy's one-pixel "zero" line
		both_ways(3, 7);

		uitest::Rig48 g;
		progress(g.frame, bar, 1, 4);
		expect_golden(self, "../ui/widgets/progress_quarter_48.pbm", g.pbm());
		uitest::Rig64 g64;
		progress(g64.frame, bar, 3, 4);
		expect_golden(self, "../ui/widgets/progress_three_quarters_64.pbm", g64.pbm());
	});

	it("clamps a progress numerator and survives a zero denominator", _ {
		uitest::Rig48 over;
		progress(over.frame, Rect{14, 20, 100, 11}, 9, 4);
		uitest::Rig48 full;
		progress(full.frame, Rect{14, 20, 100, 11}, 4, 4);
		expect(over.pbm()).to_equal(full.pbm());

		uitest::Rig48 zero;
		progress(zero.frame, Rect{14, 20, 100, 11}, 1, 0);
		uitest::Rig48 empty;
		progress(empty.frame, Rect{14, 20, 100, 11}, 0, 4);
		expect(zero.pbm()).to_equal(empty.pbm());
	});

	it("draws the framed popup's border exactly where legacy's inclusive maxima put it", _ {
		// "SAVED" is five monospaced characters: 30px wide, one 9px line, so the box is 42x21 and
		// legacy puts it at canvas (43, 13) -- `(128 - 42) / 2` and `(48 - 21) / 2`, oled.cpp:126-129.
		// The border therefore runs canvas rows 13..34, columns 43..85: 43 columns for a 42-wide
		// popup (D3), because both maxima are inclusive. SMALL rounding leaves the four corners clear.
		uitest::Rig48 r;
		popup_text(r.frame, "SAVED");

		expect(r.lit(43, 13)).to_be_false(); // corners
		expect(r.lit(85, 13)).to_be_false();
		expect(r.lit(43, 34)).to_be_false();
		expect(r.lit(85, 34)).to_be_false();

		for (int32_t x = 44; x <= 84; x++) {
			expect(r.lit(x, 13)).to_be_true();
			expect(r.lit(x, 34)).to_be_true();
		}
		for (int32_t y = 14; y <= 33; y++) {
			expect(r.lit(43, y)).to_be_true();
			expect(r.lit(85, y)).to_be_true();
		}
		expect(r.lit(86, 20)).to_be_false(); // and nothing past the right edge
		expect(r.lit(42, 20)).to_be_false();
		expect(r.lit(44, 12)).to_be_false(); // nor above the top edge
		expect(r.lit(44, 35)).to_be_false();
	});

	it("loses only its border when a popup fills the panel", _ {
		// `popup_frame` reaches OledFrame::rect_rounded, which is all-or-nothing against the clip
		// (frame.h: corner arcs have no span decomposition). Legacy clamped the box's *height* to
		// the panel (oled.cpp:122) and then drew a border whose last row fell off the canvas, so
		// for a popup that fills the panel the two differ: legacy inks three sides, this inks
		// none. The text still lands, which is what the popup is for.
		uitest::Rig64 r;
		popup_text(r.frame, "A\nB\nC\nD\nE\nF\nG\nH");
		bool any_border = false;
		for (int32_t x = 0; x < 128; x++) {
			any_border = any_border || r.lit(x, 0);
		}
		expect(any_border).to_be_false();
		bool any_text = false;
		for (int32_t y = 0; y < 64; y++) {
			for (int32_t x = 0; x < 128; x++) {
				any_text = any_text || r.lit(x, y);
			}
		}
		expect(any_text).to_be_true();
	});
});
// clang-format on

// Not CPPSPEC_SPEC(popup_w): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `ui_popup_spec`, which
// does not match the `popup_w` describe-block name the macro would assume. Spelled out by hand,
// as the other ui specs do, so the exported symbol matches what the generated driver
// (all_specs.cpp) declares `extern`.
int ui_popup_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(popup_w).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
