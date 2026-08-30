#include "ui/oled/routing.h"

#include "gfx/oled/font.h"
#include "gfx/oled/icons.h"
#include "ui/core/metrics.h"
#include "ui/oled/text.h"

#include <algorithm>

namespace deluge::ui::oled {

using gfx::oled::Rect;

namespace {

/// @brief The face legacy draws both widgets' text in.
///
/// Legacy passes `kTextSpacingX` for the advance in both blocks, but two *different* heights:
/// `kTextSizeYUpdated` (7) for the diagram's stage labels (`patch_cable_strength.cpp:91`) and
/// `kTextSpacingY` (9) for the switch's slot labels (`:154-156`). `FontId::Apple7` is the height-9
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
		// `:86-87` -- `ySpacing = kTextSpacingY`, `y = extraY + TOPMOST` then `+= 4`. The frame's
		// origin already carries TOPMOST, so `extraY + 4` is what survives.
		const int32_t y_spacing = gfx::oled::kTextSpacingY;
		int32_t y = rect.y + 1 + 4;
		stage_label(f, rect, left, y, stages[0]); // `:91`
		y += y_spacing;

		// `:126-129` -- the drop to the destination, and its arrowhead.
		f.vline(left + 3, y + 2, y + 5);
		f.icon(gfx::oled::icons::arrowDown, left + 1, y + 6);
		y += 16;

		stage_label(f, rect, left, y, stages[1]); // `:132`
		return;
	}

	// `:86-88` -- the three-stage form runs at a literal pitch of 8 and starts one row higher.
	const int32_t y_spacing = 8;
	int32_t y = rect.y + 1 + 1;
	stage_label(f, rect, left, y, stages[0]); // `:91`
	y += y_spacing;

	// `:97-101` -- the elbow from stage 0 across to stage 1, with the right-pointing arrowhead
	// sitting two rows above the horizontal.
	const int32_t elbow_y = y + (y_spacing << 1);
	f.vline(left + 4, y + 1, elbow_y);
	f.hline(elbow_y, left + 4, left + spacing_x * 2 + 4);
	f.icon(gfx::oled::icons::arrowRight, left + 3 + spacing_x, elbow_y - 2);
	y += y_spacing - 1;

	stage_label(f, rect, left + spacing_x * 2, y - 3, stages[1]); // `:105-107`
	y += y_spacing;

	// `:111-113` -- the drop from stage 1 to the destination.
	f.vline(left + spacing_x * 2 + 4, y - 2, y + 2);
	f.icon(gfx::oled::icons::arrowDown, left + spacing_x * 2 + 2, y);
	y += y_spacing;

	stage_label(f, rect, left, y, stages[2]); // `:132`
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

	// `:139-142` -- `endX = startX + <all the slot widths> - 1`, `endY = startY + kTextSpacingY`.
	const int32_t start_x = area.x;
	const int32_t start_y = area.y;
	const int32_t end_x = start_x + total - 1;
	const int32_t end_y = start_y + area.h;

	// `:145-149` -- the bracket: two verticals, a horizontal above, and a rounding pixel at each
	// top corner.
	f.vline(start_x - 2, start_y, end_y);
	f.vline(end_x + 2, start_y, end_y);
	f.hline(start_y - 2, start_x, end_x);
	f.pixel(start_x - 1, start_y - 1);
	f.pixel(end_x + 1, start_y - 1);

	// `:154-156` -- `drawStringCentered(label, x, startY, kTextSpacingX, kTextSpacingY, width)`,
	// which is what `TextStyle::truncate` ports. Note the `+ 1` on every slot but the first: see
	// the header's note on the label/inversion offset.
	int32_t x = start_x;
	for (size_t i = 0; i < n; i++) {
		text(f, Rect{x + (i == 0 ? 0 : 1), start_y, widths[i], area.h}, labels[i],
		     TextStyle{.font = kFace, .truncate = true});
		x += widths[i];
	}

	// `:157-159` -- `invertAreaRounded(x, width, startY, endY)`, whose `endY` is inclusive.
	if (selected >= 0 && static_cast<size_t>(selected) < n) {
		int32_t slot_x = start_x;
		for (int32_t i = 0; i < selected; i++) {
			slot_x += widths[static_cast<size_t>(i)];
		}
		f.invert_rounded(Rect{slot_x, start_y, widths[static_cast<size_t>(selected)], end_y - start_y + 1});
	}
}

} // namespace deluge::ui::oled
