#include "ui/oled/popup.h"

#include "ui/core/metrics.h"
#include "ui/core/text_wrap.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <cstddef>

namespace deluge::ui::oled {

using gfx::oled::Rect;
using gfx::oled::Size;

namespace {

/// `updateWorkingAnimation`'s five locals (oled.cpp:648-656). `offset` is the *positive* one;
/// the routine negates it for the saving direction at :692.
constexpr int32_t kW1 = 5;                                    // spacing between rectangles
constexpr int32_t kW2 = 5;                                    // width of the animated portion
constexpr int32_t kBarH = 8;                                  // height of the rectangle
constexpr int32_t kOffset = kW2 - 2;                          // shifting lines overlap by two pixels
constexpr int32_t kAnimWidth = kW1 + kW2 * 2 + 2;             // 17
constexpr int32_t kAnimHeight = kBarH + 1;                    // 9
constexpr int32_t kReset = kW1 + kW2 + (kBarH - 2) * kOffset; // 28, legacy's t_reset (:676)

/// One frame of the animation, in the coordinates `updateWorkingAnimation` uses, but always
/// repainting rather than relying on what the previous frame left behind. \p step is legacy's
/// `working_animation_count`.
void working_step(OledFrame& f, int32_t x_min, int32_t x_max, int32_t x2, int32_t y1, bool loading, int32_t step) {
	const int32_t y2 = y1 + kBarH - 1;

	// h2: legacy's :678-687. Step 1 fills the bar outright; later steps grow it back.
	const int32_t h2 = (step == 1) ? kBarH - 2 : std::min((step + 2) / kOffset, kBarH - 2);

	// "clears the area gradually on subsequent loops" (:689-690).
	f.clear(Rect{x_min + 1, y1 + 1, x_max - 1 - (x_min + 1) + 1, h2});

	const int32_t offset = loading ? kOffset : -kOffset;               // :691-692
	const int32_t x_pos2 = loading ? x2 - step + 1 : x_min + 1 + step; // :674

	for (int32_t i = 0; i < h2; i++) {
		int32_t x_pos = x_pos2 + i * offset;
		// backfilling lines from top to bottom (:695-698)
		if (loading && x_pos < x_min) {
			f.hline(y1 + 1 + i, x2, x_max - 1);
		}
		if (!loading && x_pos > x2) {
			f.hline(y1 + 1 + i, x_min + 1, x_min + kW2);
		}
		x_pos = std::clamp(x_pos, x_min + 1, x2);
		f.hline(y1 + 1 + i, x_pos, x_pos + kW2 - 1); // horizontally shifting lines (:701)
	}

	if (step == kReset) {
		// completed animation, just have to add final backfill line (:705-710)
		if (loading) {
			f.hline(y2 - 1, x2, x_max - 1);
		}
		else {
			f.hline(y2 - 1, x_min + 1, x_min + kW2);
		}
	}
}

/// Where the animation is in its cycle at \p elapsed ms since it was anchored, and how long the
/// current step still has to run. Derived from the two `setTimer` intervals at :711-720: step 1
/// holds for `kWorkingPauseMs`, steps 2..kReset-1 for `kWorkingStepMs` each, and step `kReset`
/// for `kWorkingPauseMs` again before the cycle restarts at step 2.
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

} // namespace

Rect popup_rect_for(OledFrame& f, Size content, PopupStyle style) {
	const Rect b = f.bounds();

	if (style == PopupStyle::Permanent) {
		// drawPermanentPopupLookingText, oled.cpp:582-586: mirrored, not centred-and-sized.
		const int32_t min_x = (b.w - content.w - kPopupMargin) >> 1;
		const int32_t min_y = (b.h - content.h - kPopupMargin) >> 1;
		return Rect{b.x + min_x, b.y + min_y, b.w - min_x * 2, b.h - min_y * 2};
	}

	// setupPopup, oled.cpp:121-129. The height clamp is legacy's own (:122); the width has none.
	const int32_t w = content.w + kPopupMargin;
	const int32_t h = std::clamp<int32_t>(content.h + kPopupMargin, 0, b.h);
	return Rect{b.x + (b.w - w) / 2, b.y + (b.h - h) / 2, w, h};
}

void popup_frame(OledFrame& f, Rect rect, PopupStyle style) {
	// `rect` carries legacy's `width`/`height`, which it passes as *inclusive* maxima; the drawn
	// extent is one pixel larger on each axis. See popup_rect_for.
	const Rect drawn{rect.x, rect.y, rect.w + 1, rect.h + 1};

	f.clear(drawn); // clearAreaExact(minX, minY, maxX, maxY), oled.cpp:131

	switch (style) {
	case PopupStyle::Framed:
		f.rect_rounded(drawn); // oled.cpp:132-135
		break;
	case PopupStyle::Notification:
		break; // `if (type != PopupType::NOTIFICATION)` -- no border
	case PopupStyle::Permanent:
		f.rect(drawn); // drawRectangle, oled.cpp:588 -- square corners
		break;
	}
}

void popup_text(OledFrame& f, std::string_view text_view, PopupStyle style) {
	const Rect b = f.bounds();
	const gfx::oled::FontId font = f.style.row_font;
	const int32_t line_h = text_height_for(font);  // kTextSpacingY
	const int32_t spacing_x = spacing_x_for(font); // kTextSpacingX

	// oled.cpp:607-613 (and :571-577 for Permanent): identical setup in both routines.
	const WrappedLines lines = wrap_lines(text_view, b.w - kPopupMargin, kPopupMaxCharsPerLine, font);

	const int32_t text_width = lines.longest;
	const int32_t text_height = static_cast<int32_t>(lines.count) * line_h;

	popup_frame(f, popup_rect_for(f, Size{text_width, text_height}, style), style);

	// textPixelY = (height - textHeight) >> 1, floored at 0 (oled.cpp:620-623 / :591-594).
	int32_t y = (b.h - text_height) >> 1;
	if (y < 0) {
		y = 0;
	}

	for (uint8_t l = 0; l < lines.count; l++) {
		// popupText skips a line that would start past the bottom (:626-628);
		// drawPermanentPopupLookingText has no such check, and neither does this for it. Legacy's
		// `continue` skips the increment too, so once past the bottom every later line is skipped.
		if (style != PopupStyle::Permanent && y >= b.h) {
			continue;
		}
		const int32_t x = (b.w - lines.widths[l]) >> 1; // :630 / :597
		f.glyphs(lines.lines[l], b.x + x, b.y + y, spacing_x, line_h);
		y += line_h;
	}
}

void working(OledFrame& f, Rect rect, std::string_view word, Blink& blink) {
	const uint32_t now = f.input.now_ms;

	// period_ms doubles as "armed", as in decimal_value: a Blink still holding state.h's 500 ms
	// default (or any other period) is anchored here, which is `working_animation_count = 1`.
	if (blink.period_ms != static_cast<uint16_t>(kWorkingStepMs)) {
		blink.period_ms = static_cast<uint16_t>(kWorkingStepMs);
		blink.until_ms = now;
	}

	const WorkingPhase phase = working_phase(now - blink.until_ms);
	f.gate.request_repaint_after(phase.remain_ms, now);

	const bool loading = (word == "Loading"); // oled.cpp:723's !strcmp(word, "Loading")

	// popupX / popupY (:654-655), against the rect instead of the panel.
	const int32_t popup_x = rect.right() - 1 - kAnimWidth;
	const int32_t popup_y = rect.y + 2;

	const int32_t x_max = popup_x + kAnimWidth;    // :669
	const int32_t x_min = x_max - (kW1 + kW2 * 2); // :670
	const int32_t x2 = x_max - kW2;                // :671
	const int32_t y1 = popup_y;                    // :672

	// The standing operations legacy performed once, at `!started_animation` and at step 1
	// (:658-662, :678-684), and then let stand: the popup's clear, the outer border, and the gap
	// punched between the two stacks. An immediate-mode frame draws them every time.
	//
	// The clear is `setupPopup`'s, one pixel wider and taller than step 1's own
	// `clearAreaExact(popupX, popupY, popupX + animation_width - 1, ...)` because `setupPopup`
	// treats `popupMinX + width` as an inclusive maximum (see popup_frame). That extra column is
	// exactly where the border's right edge lands, so step 1's narrower clear alone would leave a
	// stale pixel there on a canvas this widget does not own.
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

void progress(OledFrame& f, Rect rect, int32_t num, int32_t den) {
	if (rect.w <= 0 || rect.h <= 0) {
		return;
	}
	// A zero denominator is a caller with nothing to report; legacy divided by
	// `max_value - min_value` unguarded and never met one. `num == 0` is legacy's own
	// `min_value == 0 && value == 0` early out (number.cpp:41-44): outline, nothing else.
	if (den > 0) {
		num = std::clamp(num, 0, den);
		if (num != 0) {
			// pos_fractional * width, min_value 0 so zero_pos_horizontal is 0 (number.cpp:48-54).
			const int32_t pos =
			    static_cast<int32_t>(static_cast<float>(num) / static_cast<float>(den) * static_cast<float>(rect.w));
			// The fill spans y_top + 1 .. y_top + height - 1, height being rect.h - 1 (:59-70).
			// A non-zero value too small for one column still inks one, which is legacy's
			// "= 0, draw vertical line in the middle" arm reached at zero_pos_horizontal == 0.
			const Rect band{rect.x, rect.y + 1, pos > 0 ? pos : 1, rect.h - 2};
			if (band.h > 0) {
				f.invert(band);
			}
		}
	}

	// The outline is drawn *after* the fill, as number.cpp:72 does: it is an OR, not an invert,
	// so drawing it first and then inverting across it would punch holes in the sides.
	f.rect_rounded(rect);
}

} // namespace deluge::ui::oled
