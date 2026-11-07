#include "ui/oled/routing.h"

#include "gfx/oled/font.h"
#include "gfx/oled/icons.h"
#include "ui/core/blink.h"
#include "ui/core/metrics.h"
#include "ui/oled/detail/int_to_string.h"
#include "ui/oled/text.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <span>

namespace deluge::ui::oled {

using gfx::oled::Rect;

namespace {

/// @brief `Decimal::getNumNonZeroDecimals`, exactly: how many of @p value's two fraction places are not trailing zeros.
int32_t non_zero_decimals(int32_t value) {
	const int32_t fraction = std::abs(value % 100);
	if (fraction == 0) {
		return 0;
	}
	return fraction % 10 == 0 ? 1 : 2;
}

/// @brief `10^exponent` for the four places a strength has.
int32_t place_value(int32_t exponent) {
	int32_t v = 1;
	for (int32_t i = 0; i < exponent; i++) {
		v *= 10;
	}
	return v;
}

/// @brief The face legacy draws both widgets' text in.
///
/// Legacy passes `kTextSpacingX` for the advance in both blocks, but two *different* heights:
/// `kTextSizeYUpdated` (7) for the diagram's stage labels (`patch_cable_strength.cpp:91`) and
/// `kTextSpacingY` (9) for the switch's slot labels (`:143-145`). `FontId::Apple7` is the height-9
/// spelling (`ui/core/metrics.h`'s `text_height_of` note), so only the diagram needs
/// `kApple7Height7YAdjust` -- which is exactly why that constant is caller-side, and why this file
/// applies it in one place and not the other.
constexpr gfx::oled::FontId kFace = gfx::oled::FontId::Apple7;

/// @brief `Canvas::drawString(s, x, y, kTextSpacingX, kTextSizeYUpdated)` at a legacy height of 7.
void stage_label(OledFrame& f, Rect rect, int32_t x, int32_t y, std::string_view s) {
	text(f, Rect{x, y + kApple7Height7YAdjust, rect.right() - x, gfx::oled::kTextSpacingY}, s,
	     TextStyle{.font = kFace});
}

} // namespace

void routing_chain(OledFrame& f, Rect rect, std::span<const std::string_view> stages) {
	const size_t n = std::min(stages.size(), kMaxRoutingStages);
	if (n < 2) {
		return;
	}

	// `constexpr int32_t leftPadding = 2;` (`patch_cable_strength.cpp:85`), against the rect's left
	// edge rather than the panel's.
	const int32_t left = rect.x + 2;
	const int32_t spacing_x = gfx::oled::kTextSpacingX;

	if (n == 2) {
		// `:86-88` -- `ySpacing = kTextSpacingY`, `y = extraY + TOPMOST` then `+= 4`. The frame's
		// origin already carries TOPMOST, so `extraY + 4` is what survives.
		const int32_t y_spacing = gfx::oled::kTextSpacingY;
		int32_t y = rect.y + 1 + 4;
		stage_label(f, rect, left, y, stages[0]); // `:91`
		y += y_spacing;

		// `:113-115` -- the drop to the destination, and its arrowhead.
		f.vline(left + 3, y + 2, y + 5);
		f.icon(gfx::oled::icons::arrowDown, left + 1, y + 6);
		y += 16;

		stage_label(f, rect, left, y, stages[1]); // `:120-122`
		return;
	}

	// `:86-88` -- the three-stage form runs at a literal pitch of 8 and starts one row higher.
	const int32_t y_spacing = 8;
	int32_t y = rect.y + 1 + 1;
	stage_label(f, rect, left, y, stages[0]); // `:91`
	y += y_spacing;

	// `:96-99` -- the elbow from stage 0 across to stage 1, with the right-pointing arrowhead
	// sitting two rows above the horizontal.
	const int32_t elbow_y = y + (y_spacing << 1);
	f.vline(left + 4, y + 1, elbow_y);
	f.hline(elbow_y, left + 4, left + spacing_x * 2 + 4);
	f.icon(gfx::oled::icons::arrowRight, left + 3 + spacing_x, elbow_y - 2);
	y += y_spacing - 1;

	stage_label(f, rect, left + spacing_x * 2, y - 3, stages[1]); // `:103-104`
	y += y_spacing;

	// `:108-109` -- the drop from stage 1 to the destination.
	f.vline(left + spacing_x * 2 + 4, y - 2, y + 2);
	f.icon(gfx::oled::icons::arrowDown, left + spacing_x * 2 + 2, y);
	y += y_spacing;

	stage_label(f, rect, left, y, stages[2]); // `:120-122`
}

void segmented_switch(OledFrame& f, Rect area, std::span<const std::string_view> labels,
                      std::span<const int32_t> widths, int32_t selected) {
	const size_t n = std::min(labels.size(), widths.size());
	if (n == 0) {
		return;
	}
	int32_t total = 0;
	for (size_t i = 0; i < n; i++) {
		total += widths[i];
	}

	// `:130-132` -- `endX = startX + <all the slot widths> - 1`, `endY = startY + kTextSpacingY`.
	const int32_t start_x = area.x;
	const int32_t start_y = area.y;
	const int32_t end_x = start_x + total - 1;
	const int32_t end_y = start_y + area.h;

	// `:135-139` -- the bracket: two verticals, a horizontal above, and a rounding pixel at each
	// top corner.
	f.vline(start_x - 2, start_y, end_y);
	f.vline(end_x + 2, start_y, end_y);
	f.hline(start_y - 2, start_x, end_x);
	f.pixel(start_x - 1, start_y - 1);
	f.pixel(end_x + 1, start_y - 1);

	// `:143-145` -- `drawStringCentered(label, x, startY, kTextSpacingX, kTextSpacingY, width)`,
	// which is what `TextStyle::truncate` ports. Note the `+ 1` on every slot but the first: see
	// the header's note on the label/inversion offset.
	int32_t x = start_x;
	for (size_t i = 0; i < n; i++) {
		text(f, Rect{x + (i == 0 ? 0 : 1), start_y, widths[i], area.h}, labels[i],
		     TextStyle{.font = kFace, .truncate = true});
		x += widths[i];
	}

	// `:147-149` -- `invertAreaRounded(x, width, startY, endY)`, whose `endY` is inclusive.
	if (selected >= 0 && static_cast<size_t>(selected) < n) {
		int32_t slot_x = start_x;
		for (int32_t i = 0; i < selected; i++) {
			slot_x += widths[static_cast<size_t>(i)];
		}
		f.invert_rounded(Rect{slot_x, start_y, widths[static_cast<size_t>(selected)], end_y - start_y + 1});
	}
}

Response cable_strength_value(OledFrame& f, Rect rect, int32_t& value, Range range, int8_t& edit_pos, Blink& blink,
                              bool take_turn) {
	Response resp{};
	resp.focused = true;
	const uint32_t now = f.now_ms();
	edit_pos = static_cast<int8_t>(std::max<int32_t>(0, edit_pos));

	// `Decimal::horizontalEncoderAction`: right to a lower place, left while the next place's step fits the maximum.
	f.claim_horizontal();
	f.claim_turn();
	const int8_t horizontal = f.horizontal();
	if (horizontal > 0 && edit_pos > 0) {
		edit_pos--;
	}
	else if (horizontal < 0 && static_cast<int64_t>(place_value(edit_pos)) * 10 <= range.max) {
		edit_pos++;
	}
	// `Decimal::selectEncoderAction`, with `SoundEditor::selectEncoderAction`'s 5x under SHIFT.
	const int8_t turn = f.turn();
	if (take_turn && turn != 0) {
		const int64_t detents = f.shift() ? int64_t{turn} * 5 : int64_t{turn};
		const int64_t next =
		    std::clamp<int64_t>(int64_t{value} + detents * place_value(edit_pos), range.min, range.max);
		if (next != value) {
			value = static_cast<int32_t>(next);
			resp.changed = true;
			f.gate().request_repaint();
		}
	}

	// `OLED::setupBlink` restarts on every render: lit at once for a cursor move (`movingCursor`), dark otherwise.
	if (!blink.armed()) {
		blink.period_ms = kFlashTime;
		blink.restart_dark(now);
	}
	if (horizontal != 0) {
		blink.restart_lit(now);
	}
	else if (resp.changed) {
		blink.restart_dark(now);
	}
	f.gate().request_repaint_after(blink_next_flip_in(blink, now), now);

	// `:170-176` -- the dropped places are zeros, so the division is exact.
	const int32_t hidden = std::clamp<int32_t>(2 - non_zero_decimals(value), 0, edit_pos);
	const int32_t shown_pos = edit_pos - hidden;
	std::array<char, 12> buf{};
	const std::string_view digits = detail::int_to_string(value / place_value(hidden), 3 - hidden, std::span{buf});

	constexpr int32_t kDigitWidth = kTextBigSpacingX;
	constexpr int32_t kDigitHeight = gfx::oled::kTextBigSizeY;
	constexpr int32_t kRightPadding = 3;
	const int32_t right = rect.right() - kRightPadding;
	const int32_t under = rect.y + kDigitHeight + 1;

	f.clear(rect);
	// `:181-183` -- `drawStringAlignRight(numberBuf, textY, digitWidth, digitHeight, OLED_MAIN_WIDTH_PIXELS - 3)`.
	text(f, Rect{rect.x, rect.y, right - rect.x, kDigitHeight}, digits,
	     TextStyle{.font = gfx::oled::FontId::MetricBold13, .align = Align::Right});
	// `:189-194` -- the tick, two columns at the fraction's left edge, three rows under the digits.
	if (const int32_t fraction_places = 2 - hidden; fraction_places > 0) {
		const int32_t separator_x = right - fraction_places * kDigitWidth;
		f.vline(separator_x, under, under + 2);
		f.vline(separator_x - 1, under, under + 2);
	}
	// `:185-187` -- the cursor, one row under the edited place, inverted over whatever is drawn there.
	if (blink_lit(blink, now)) {
		f.invert(Rect{right - (shown_pos + 1) * kDigitWidth, under, kDigitWidth, 1});
	}
	return resp;
}

} // namespace deluge::ui::oled
