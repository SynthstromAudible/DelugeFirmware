#include "ui/oled/popup.h"

#include "ui/core/metrics.h"
#include "ui/core/text_wrap.h"
#include "ui/oled/scroll_text.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <cstddef>

namespace deluge::ui::oled {

using gfx::oled::Rect;
using gfx::oled::Size;

namespace {

/// Matches legacy's five animation constants. `kOffset` is the *positive* shift; the routine
/// negates it for the saving direction.
constexpr int32_t kW1 = 5;                                    // spacing between rectangles
constexpr int32_t kW2 = 5;                                    // width of the animated portion
constexpr int32_t kBarH = 8;                                  // height of the rectangle
constexpr int32_t kOffset = kW2 - 2;                          // shifting lines overlap by two pixels
constexpr int32_t kAnimWidth = kW1 + kW2 * 2 + 2;             // 17
constexpr int32_t kAnimHeight = kBarH + 1;                    // 9
constexpr int32_t kReset = kW1 + kW2 + (kBarH - 2) * kOffset; // 28, matching legacy's t_reset

/// One frame of the animation, in the coordinates `updateWorkingAnimation` uses, but always
/// repainting rather than relying on what the previous frame left behind. `step` is legacy's
/// `working_animation_count`.
void working_step(OledFrame& f, int32_t x_min, int32_t x_max, int32_t x2, int32_t y1, bool loading, int32_t step) {
	const int32_t y2 = y1 + kBarH - 1;

	// h2: step 1 fills the bar outright; later steps grow it back.
	const int32_t h2 = (step == 1) ? kBarH - 2 : std::min((step + 2) / kOffset, kBarH - 2);

	// Clears the area gradually on subsequent loops.
	f.clear(Rect{x_min + 1, y1 + 1, x_max - 1 - (x_min + 1) + 1, h2});

	const int32_t offset = loading ? kOffset : -kOffset;
	const int32_t x_pos2 = loading ? x2 - step + 1 : x_min + 1 + step;

	for (int32_t i = 0; i < h2; i++) {
		int32_t x_pos = x_pos2 + i * offset;
		// backfilling lines from top to bottom
		if (loading && x_pos < x_min) {
			f.hline(y1 + 1 + i, x2, x_max - 1);
		}
		if (!loading && x_pos > x2) {
			f.hline(y1 + 1 + i, x_min + 1, x_min + kW2);
		}
		x_pos = std::clamp(x_pos, x_min + 1, x2);
		f.hline(y1 + 1 + i, x_pos, x_pos + kW2 - 1); // horizontally shifting lines
	}

	if (step == kReset) {
		// completed animation, just have to add final backfill line
		if (loading) {
			f.hline(y2 - 1, x2, x_max - 1);
		}
		else {
			f.hline(y2 - 1, x_min + 1, x_min + kW2);
		}
	}
}

/// Where the animation is in its cycle at `elapsed` ms since it was anchored, and how long the
/// current step still has to run. Step 1 holds for `kWorkingPauseMs`, steps 2..kReset-1 for
/// `kWorkingStepMs` each, and step `kReset` for `kWorkingPauseMs` again before the cycle restarts
/// at step 2.
struct WorkingPhase {
	int32_t step;
	uint32_t cycle;     // 1 while the first pass is running, 2+ afterwards
	uint32_t remain_ms; // until the next step
};

WorkingPhase working_phase(uint32_t elapsed) {
	constexpr uint32_t pause = static_cast<uint32_t>(kWorkingPauseMs);
	constexpr uint32_t stepms = static_cast<uint32_t>(kWorkingStepMs);
	constexpr uint32_t run = static_cast<uint32_t>(kReset - 2) * stepms; // steps 2..kReset-1
	constexpr uint32_t cycle_len = run + pause;                          // + the long final step

	if (elapsed < pause) {
		return WorkingPhase{1, 1, pause - elapsed};
	}
	const uint32_t e = elapsed - pause;
	const uint32_t cycle = 1 + e / cycle_len;
	const uint32_t r = e % cycle_len;
	if (r < run) {
		return WorkingPhase{static_cast<int32_t>(2 + r / stepms), cycle, stepms - (r % stepms)};
	}
	return WorkingPhase{kReset, cycle, cycle_len - r};
}

/// The whole canvas, expressed in `f`'s local coordinates: `{-origin.x, -origin.y, width,
/// height}`.
///
/// Every popup coordinate legacy computes is against `OLED_MAIN_WIDTH_PIXELS` /
/// `OLED_MAIN_HEIGHT_PIXELS` -- the *panel*, hidden bezel rows included -- so all of the
/// arithmetic below is done in this rectangle and lands in frame coordinates for free. The same
/// canvas-absolute conversion is needed elsewhere in this layer for other panel-height-dependent
/// layout, hoisted into one place because five call sites want it. See popup.h.
Rect panel_of(const OledFrame& f) {
	const gfx::oled::Geometry g = f.canvas.geometry();
	return Rect{-f.origin.x, -f.origin.y, g.width, g.height};
}

} // namespace

Rect popup_rect_for(OledFrame& f, Size content, PopupStyle style) {
	const Rect p = panel_of(f);

	if (style == PopupStyle::Permanent) {
		// Matches legacy's drawPermanentPopupLookingText: mirrored, not centred-and-sized.
		const int32_t min_x = (p.w - content.w - kPopupMargin) >> 1;
		const int32_t min_y = (p.h - content.h - kPopupMargin) >> 1;
		return Rect{p.x + min_x, p.y + min_y, p.w - min_x * 2, p.h - min_y * 2};
	}

	// Matches legacy's setupPopup. The height clamp is legacy's own; the width has none.
	const int32_t w = content.w + kPopupMargin;
	const int32_t h = std::clamp<int32_t>(content.h + kPopupMargin, 0, p.h);
	return Rect{p.x + (p.w - w) / 2, p.y + (p.h - h) / 2, w, h};
}

void popup_frame(OledFrame& f, Rect rect, PopupStyle style) {
	// `rect` carries legacy's `width`/`height`, which it passes as *inclusive* maxima; the drawn
	// extent is one pixel larger on each axis. See popup_rect_for.
	const Rect drawn{rect.x, rect.y, rect.w + 1, rect.h + 1};

	f.clear(drawn); // matches legacy's clearAreaExact(minX, minY, maxX, maxY)

	switch (style) {
	case PopupStyle::Framed:
		f.rect_rounded(drawn);
		break;
	case PopupStyle::Notification:
		break; // no border
	case PopupStyle::Permanent:
		f.rect(drawn); // square corners
		break;
	}
}

void popup_text(OledFrame& f, std::string_view text_view, PopupStyle style) {
	const Rect p = panel_of(f);
	const gfx::oled::FontId font = f.style.row_font;
	const int32_t line_h = text_height_for(font);  // kTextSpacingY
	const int32_t spacing_x = spacing_x_for(font); // kTextSpacingX

	// Same wrap setup legacy uses for both the Framed/Notification and Permanent routines.
	const WrappedLines lines = wrap_lines(text_view, p.w - kPopupMargin, kPopupMaxCharsPerLine, font);

	const int32_t text_width = lines.longest;
	const int32_t text_height = static_cast<int32_t>(lines.count) * line_h;

	popup_frame(f, popup_rect_for(f, Size{text_width, text_height}, style), style);

	// `y` stays canvas-absolute through the loop, as legacy's does, because the bottom check
	// below is against the panel; `p.y` converts it at the draw call.
	int32_t y = (p.h - text_height) >> 1;
	if (y < 0) {
		y = 0;
	}

	for (uint8_t l = 0; l < lines.count; l++) {
		// Legacy's popupText skips a line that would start past the bottom;
		// drawPermanentPopupLookingText has no such check, and neither does this for it. Legacy's
		// `continue` skips the increment too, so once past the bottom every later line is skipped.
		if (style != PopupStyle::Permanent && y >= p.h) {
			continue;
		}
		const int32_t x = (p.w - lines.widths[l]) >> 1;
		f.glyphs(lines.lines[l], p.x + x, p.y + y, spacing_x, line_h);
		y += line_h;
	}
}

namespace {

// `ContextMenu::renderOLED`'s literals (`gui/context_menu/context_menu.cpp`). The four "inset"
// numbers are all `OLED_MAIN_WIDTH_PIXELS - n` in legacy and are applied to the *panel* width
// here, so a wider panel widens the window's furniture with it rather than leaving it hanging.
constexpr int32_t kOptionWindowW = 100;
constexpr int32_t kOptionWindowH = 40;
constexpr int32_t kOptionHeadingX = 22;         // `drawString(getTitle(), 22, ...)`
constexpr int32_t kOptionHeadingDy = 6;         // `windowMinY + 6`
constexpr int32_t kOptionRuleDy = 15;           // `drawHorizontalLine(windowMinY + 15, ...)`
constexpr int32_t kOptionRuleRightInset = 30;   // `OLED_MAIN_WIDTH_PIXELS - 30`, inclusive end
constexpr int32_t kOptionFirstRowDy = 18;       // `textPixelY = windowMinY + 18`
constexpr int32_t kOptionInvertX = 22;          // `invertStartX`
constexpr int32_t kOptionInvertRightInset = 44; // highlight WIDTH is `OLED_MAIN_WIDTH_PIXELS - 44`
constexpr int32_t kOptionTextRightInset = 27;   // `drawString(..., OLED_MAIN_WIDTH_PIXELS - 27)`
constexpr int32_t kOptionFooterDy = 20;         // `textPixelY = windowMinY + 20 + kTextSpacingY`
constexpr int32_t kOptionFooterRightInset = 26; // `drawString(..., OLED_MAIN_WIDTH_PIXELS - 26)`

// `Slicer::renderOLED`'s literals (`gui/ui/slicer.cpp:76-97`). Same treatment as the option-window
// block above: the one "inset" number is `OLED_MAIN_WIDTH_PIXELS - n` in legacy and is applied to
// the panel width here.
constexpr int32_t kCountWindowW = 100;       // `windowWidth = 100`
constexpr int32_t kCountWindowH = 31;        // `windowHeight = 31`
constexpr int32_t kCountWindowShiftX = 6;    // `horizontalShift`, added to `windowMinX`
constexpr int32_t kCountWindowShiftY = 2;    // `windowMinY += 2`
constexpr int32_t kCountHeadingX = 30;       // `drawString("Num. slices", 30, ...)`
constexpr int32_t kCountHeadingDy = 6;       // `windowMinY + 6`
constexpr int32_t kCountRuleDy = 15;         // `drawHorizontalLine(windowMinY + 15, ...)`
constexpr int32_t kCountRuleX = 26;          // that line's `startX`, NOT the window's left edge
constexpr int32_t kCountRuleRightInset = 22; // `OLED_MAIN_WIDTH_PIXELS - 22`, inclusive end
constexpr int32_t kCountValueDy = 18;        // `drawStringCentred(buffer, windowMinY + 18, ...)`

} // namespace

void option_window(OledFrame& f, std::string_view heading, std::span<const std::string_view> options, int32_t selected,
                   int32_t first, ScrollText& label, bool allow_motion, std::string_view footer) {
	const Rect p = panel_of(f);
	const gfx::oled::FontId font = f.style.row_font;
	const int32_t line_h = text_height_for(font);  // kTextSpacingY
	const int32_t spacing_x = spacing_x_for(font); // kTextSpacingX

	// `windowMinX/Y` and the mirrored maxima, verbatim. The rect carries legacy's width/height,
	// which `popup_frame` treats as inclusive maxima -- so the outline it draws is
	// `drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY)` exactly (see popup_rect_for).
	// `Permanent` because legacy's `drawRectangle` is the square primitive; its clear covers the
	// border ring as well as the interior legacy's `clearAreaExact` clears, which is the same
	// result once the ring is inked back in on the next line.
	const int32_t min_x = (p.w - kOptionWindowW) >> 1;
	const int32_t min_y = (p.h - kOptionWindowH) >> 1;
	popup_frame(f, Rect{p.x + min_x, p.y + min_y, kOptionWindowW, kOptionWindowH}, PopupStyle::Permanent);

	f.hline(p.y + min_y + kOptionRuleDy, p.x + kOptionHeadingX, p.x + p.w - kOptionRuleRightInset);

	// `drawString(getTitle(), 22, windowMinY + 6, kTextSpacingX, kTextSpacingY)` -- left-aligned,
	// in the row font, with legacy's default (panel-edge) right clip. Not `title()`: there is no
	// screen title band here and no `drawScreenTitle` call.
	text(f, Rect{p.x + kOptionHeadingX, p.y + min_y + kOptionHeadingDy, p.w - kOptionHeadingX, line_h}, heading,
	     TextStyle{.font = font});

	const int32_t n = static_cast<int32_t>(options.size());
	if (n > 0) {
		const int32_t cursor = std::clamp<int32_t>(selected, 0, n - 1);
		// Keep the focused row inside the window, and never start past the end of the list. Legacy's
		// own `scrollPos` invariant, restated defensively rather than trusted: `selectEncoderAction`
		// maintains it, but `setupAndCheckAvailability` and a caller restoring saved state both write
		// the pair directly.
		const int32_t start =
		    std::clamp<int32_t>(first, std::max<int32_t>(0, cursor - (kOptionWindowRows - 1)), cursor);

		const int32_t invert_x = p.x + kOptionInvertX;
		const int32_t invert_w = p.w - kOptionInvertRightInset;
		// `textPixelX = invertStartX + 1`, plus one character cell in legacy's NO_INVERSION mode.
		const int32_t text_x = invert_x + 1 + (f.style.invert_highlight ? 0 : spacing_x);
		const int32_t text_end = p.x + p.w - kOptionTextRightInset;

		for (int32_t o = 0; o < kOptionWindowRows && start + o < n; o++) {
			const int32_t y = p.y + min_y + kOptionFirstRowDy + o * line_h;
			const Rect box{text_x, y, text_end - text_x, line_h};
			if (start + o != cursor) {
				text(f, box, options[static_cast<size_t>(start + o)], TextStyle{.font = font});
				continue;
			}
			// The focused row: legacy draws the string, then inverts, then arms the side scroller over
			// the same box. `scroll_text` is the draw and the arming in one call; the inversion is
			// separate because legacy's is a *different, wider* rectangle than the text box, which
			// `scroll_text`'s own `highlight` flag cannot express.
			scroll_text(f, box, options[static_cast<size_t>(cursor)], font, label, allow_motion, /*highlight=*/false);
			if (f.style.invert_highlight) {
				f.invert_rounded(Rect{invert_x, y, invert_w, 9});
			}
			else {
				f.vline(invert_x, y, y + 8);
			}
		}
	}

	// `AudioInputSelector::renderOLED` (`audio_input_selector.cpp:222-237`) calls the base renderer
	// and *then* draws this line, so it is drawn last here too -- ordering, not "different rows", is
	// what makes it safe, and matching legacy's ordering is what stops that from mattering. It is
	// not cleared first, exactly as legacy's `drawString` is not. Its left origin is
	// `kOptionHeadingX`, not the option rows' `invertStartX + 1`, and its right clip is one pixel
	// wider than theirs.
	if (!footer.empty()) {
		const int32_t footer_y = p.y + min_y + kOptionFooterDy + line_h;
		text(f, Rect{p.x + kOptionHeadingX, footer_y, p.w - kOptionFooterRightInset - kOptionHeadingX, line_h}, footer,
		     TextStyle{.font = font});
	}
}

void working(OledFrame& f, Rect rect, std::string_view word, Blink& blink) {
	const uint32_t now = f.now_ms();

	// period_ms doubles as "armed", as in decimal_value: a Blink still holding state.h's 500 ms
	// default (or any other period) is anchored here, restarting the animation from its first step.
	if (blink.period_ms != static_cast<uint16_t>(kWorkingStepMs)) {
		blink.period_ms = static_cast<uint16_t>(kWorkingStepMs);
		blink.until_ms = now;
	}

	const WorkingPhase phase = working_phase(now - blink.until_ms);
	f.gate().request_repaint_after(phase.remain_ms, now);

	const bool loading = (word == "Loading");

	// Anchored against the rect instead of the panel.
	const int32_t popup_x = rect.right() - 1 - kAnimWidth;
	const int32_t popup_y = rect.y + 2;

	const int32_t x_max = popup_x + kAnimWidth;
	const int32_t x_min = x_max - (kW1 + kW2 * 2);
	const int32_t x2 = x_max - kW2;
	const int32_t y1 = popup_y;

	// Legacy performed these operations once and then let them stand: the popup's clear, the
	// outer border, and the gap punched between the two stacks. An immediate-mode frame draws
	// them every time instead.
	//
	// The clear is one pixel wider and taller than the bar itself, because legacy treats
	// `popupMinX + width` as an inclusive maximum (see popup_frame). That extra column is exactly
	// where the border's right edge lands, so a narrower clear alone would leave a stale pixel
	// there on a canvas this widget does not own.
	f.clear(Rect{popup_x, popup_y, kAnimWidth + 1, kAnimHeight + 1});
	f.rect(Rect{x_min, y1, x_max - x_min + 1, kBarH});
	f.clear(Rect{x_min + kW2 + 1, popup_y, (x_max - kW2 - 1) - (x_min + kW2 + 1) + 1, kAnimHeight});

	// Replay the history the persistent canvas would have held: the initialiser, then -- for any
	// cycle past the first -- the cycle-ending frame, which repaints all six interior rows and so
	// erases everything before it, then this cycle's steps.
	working_step(f, x_min, x_max, x2, y1, loading, 1);
	if (phase.cycle >= 2) {
		working_step(f, x_min, x_max, x2, y1, loading, kReset);
	}
	for (int32_t s = 2; s <= phase.step; s++) {
		working_step(f, x_min, x_max, x2, y1, loading, s);
	}
}

namespace {
/// `etl::string<25>` -- `OLED::displayNotification`'s own `titleBuf` capacity (`oled.cpp:745`).
constexpr size_t kNotificationTitleCap = 25;
} // namespace

void notification(OledFrame& f, std::string_view title, std::string_view value) {
	const gfx::oled::FontId font = f.style.row_font;
	const int32_t line_h = text_height_for(font);  // kTextSpacingY
	const int32_t spacing_x = spacing_x_for(font); // kTextSpacingX

	constexpr int32_t padding_left = 4;
	const int32_t start_x = 0;
	const int32_t start_y = 0; // OLED_MAIN_TOPMOST_PIXEL, frame-local (see the note on title()).
	const int32_t end_y = start_y + line_h + 1;
	const int32_t panel_w = f.bounds().w;
	const int32_t end_x = panel_w - 1;
	const int32_t width = end_x - start_x + 1; // == panel_w
	const int32_t height = end_y - start_y;

	// `titleBuf`: param_title copied and capped to 25 characters (`etl::string<25>`).
	std::string_view title_buf = title.substr(0, std::min(title.size(), kNotificationTitleCap));

	// Measured from the *uncapped* title, matching legacy's own order (see the doc note).
	int32_t title_width = measure(title, font).w;
	const int32_t value_width = value.empty() ? 0 : measure(value, font).w;

	if (value_width > 0) {
		while (title_width + padding_left + value_width > panel_w - 7 && !title_buf.empty()) {
			title_buf.remove_suffix(1);
			title_width = measure(title_buf, font).w;
		}
	}

	popup_frame(f, Rect{start_x, start_y, width - 1, height}, PopupStyle::Notification);

	const bool no_inversion = !f.style.invert_highlight;
	const int32_t title_start_x = padding_left;
	const int32_t title_start_y = no_inversion ? start_y : start_y + 1;
	f.glyphs(title_buf, title_start_x, title_start_y, spacing_x, line_h);

	if (value_width > 0) {
		f.glyph(':', title_start_x + title_width, title_start_y, spacing_x, line_h);
		f.glyphs(value, title_start_x + title_width + 8, title_start_y, spacing_x, line_h);
	}

	if (no_inversion) {
		for (int32_t x = start_x + 1; x < end_x; x += 2) {
			f.pixel(x, end_y);
		}
	}
	else {
		f.invert_rounded(Rect{start_x, start_y, width, end_y - start_y + 1});
		f.pixel(start_x, start_y);
		f.pixel(end_x, start_y);
	}
}

void count_window(OledFrame& f, std::string_view heading, std::string_view value) {
	const Rect p = panel_of(f);
	const gfx::oled::FontId font = f.style.row_font;
	const int32_t line_h = text_height_for(font); // kTextSpacingY

	// `windowMinX = ((OLED_MAIN_WIDTH_PIXELS - windowWidth) >> 1) + horizontalShift` and
	// `windowMinY = ((OLED_MAIN_HEIGHT_PIXELS - windowHeight) >> 1) + 2`, verbatim. The rect
	// carries legacy's width/height, which `popup_frame` treats as inclusive maxima -- so the
	// outline it draws is `drawRectangle(windowMinX, windowMinY, windowMaxX, windowMaxY)` exactly
	// (see popup_rect_for). `Permanent` because legacy's `drawRectangle` is the square primitive.
	const int32_t min_x = ((p.w - kCountWindowW) >> 1) + kCountWindowShiftX;
	const int32_t min_y = ((p.h - kCountWindowH) >> 1) + kCountWindowShiftY;
	popup_frame(f, Rect{p.x + min_x, p.y + min_y, kCountWindowW, kCountWindowH}, PopupStyle::Permanent);

	// `drawHorizontalLine(windowMinY + 15, 26, OLED_MAIN_WIDTH_PIXELS - 22)`. Note that neither
	// endpoint is derived from the window: the rule is inset 6 from the box on the left and 14 on
	// the right, because `horizontalShift` moved the box and not this line. See the header.
	f.hline(p.y + min_y + kCountRuleDy, p.x + kCountRuleX, p.x + p.w - kCountRuleRightInset);

	// `drawString("Num. slices", 30, windowMinY + 6, kTextSpacingX, kTextSpacingY)` -- left-aligned
	// in the row font, with legacy's default (panel-edge) right clip.
	text(f, Rect{p.x + kCountHeadingX, p.y + min_y + kCountHeadingDy, p.w - kCountHeadingX, line_h}, heading,
	     TextStyle{.font = font});

	// `drawStringCentred(buffer, windowMinY + 18, kTextSpacingX, kTextSpacingY,
	//                    (OLED_MAIN_WIDTH_PIXELS >> 1) + horizontalShift)`.
	// `Align::Centre` is `rect.x + rect.w / 2 - (width >> 1)` and legacy's is
	// `centrePos - (stringWidth >> 1)`, so the rect is built to put its own centre exactly on
	// `centrePos`: origin at the panel's left edge, width twice the centre. `rect.w` is even, so
	// the halving is exact.
	const int32_t centre = (p.w >> 1) + kCountWindowShiftX;
	text(f, Rect{p.x, p.y + min_y + kCountValueDy, centre * 2, line_h}, value,
	     TextStyle{.font = font, .align = Align::Centre});
}

void progress(OledFrame& f, Rect rect, int32_t num, int32_t den) {
	if (rect.w <= 0 || rect.h <= 0) {
		return;
	}
	// A zero denominator is a caller with nothing to report; legacy divided unguarded and never
	// met one. `num == 0` is legacy's own early out: outline, nothing else.
	if (den > 0) {
		num = std::clamp(num, 0, den);
		if (num != 0) {
			const int32_t pos =
			    static_cast<int32_t>(static_cast<float>(num) / static_cast<float>(den) * static_cast<float>(rect.w));
			// A non-zero value too small for one column still inks one, which is legacy's own
			// "draw vertical line in the middle" arm for a zero fill width.
			const Rect band{rect.x, rect.y + 1, pos > 0 ? pos : 1, rect.h - 2};
			if (band.h > 0) {
				f.invert(band);
			}
		}
	}

	// The outline is drawn *after* the fill, as legacy does: it is an OR, not an invert, so
	// drawing it first and then inverting across it would punch holes in the sides.
	f.rect_rounded(rect);
}

} // namespace deluge::ui::oled
