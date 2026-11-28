#include "ui/views/oled/rename.h"

#include "ui/gfx/oled/geometry.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/oled/popup.h"
#include "ui/widgets/oled/text.h"

namespace deluge::ui::views::oled {

namespace {

using gfx::oled::Rect;
using widgets::core::kTextSpacingX;
using widgets::core::kTextSpacingY;

/// The whole canvas in @p f's local coordinates.
///
/// Every coordinate `RenameUI::renderOLED` computes is against `OLED_MAIN_WIDTH_PIXELS` / `OLED_MAIN_HEIGHT_PIXELS` --
/// the *panel*, the rows the faceplate hides included -- so the arithmetic below is done in this rectangle and lands
/// in frame coordinates for free. Same conversion `popup.cpp`'s own `panel_of` makes, for the same reason; it is
/// file-static there.
Rect panel_of(const widgets::oled::OledFrame& f) {
	const gfx::oled::Geometry g = f.canvas.geometry();
	return Rect{-f.origin.x, -f.origin.y, g.width, g.height};
}

// `RenameUI::renderOLED` (`rename_ui.cpp`), as named constants.
constexpr int32_t kWindowW = 120;
constexpr int32_t kWindowH = 40;
/// `maxNumChars`, legacy's own literal: "'RENAME INSTRUMENT' should be the longest title string".
constexpr int32_t kMaxNumChars = 17;
/// Legacy's title row, `windowMinY + 6`.
constexpr int32_t kTitleDy = 6;
/// Legacy's name box, canvas rows 24..38 with `windowMinY == 4` on the shipped 48-row panel -- i.e. `windowMinY + 20`
/// and `windowMinY + 34`. Expressed relative to the window rather than canvas-absolute, because the two spellings
/// agree exactly on the shipped geometry and only the relative one survives a taller panel: at 64 rows `windowMinY` is
/// 12 and legacy's absolute 24 would put the box *through* its own title.
constexpr int32_t kBoxDy = 20;
constexpr int32_t kBoxH = 14;
/// Legacy's `yPixel`, canvas row 27 -- three rows into the box, on either geometry.
constexpr int32_t kTextDy = kBoxDy + 3;

} // namespace

void rename_window(widgets::oled::OledFrame& f, const RenameWindow& d, widgets::oled::TextEditState& edit) {
	// Drawn on the whole panel: legacy's window starts at row 4 of the 48-row panel, above the root frame's
	// `visible_top`.
	widgets::oled::OledFrame pf = widgets::oled::panel_frame(f);
	const Rect panel = panel_of(pf);

	// The window: 120x40, centred on the panel. `popup_frame` is `clearAreaExact` + `drawRectangle` in one call, and
	// `PopupStyle::Permanent` is its square-cornered arm -- legacy draws a square rectangle here, not a rounded one
	// (`popup.h`).
	const int32_t win_x = panel.x + ((panel.w - kWindowW) >> 1);
	const int32_t win_y = panel.y + ((panel.h - kWindowH) >> 1);
	widgets::oled::popup_frame(pf, Rect{win_x, win_y, kWindowW, kWindowH}, widgets::oled::PopupStyle::Permanent);

	// `drawStringCentred(title, windowMinY + 6, kTextSpacingX, kTextSpacingY)` -- centred on the *panel*, in the
	// ordinary row font, NOT `drawScreenTitle`: there is no title rule here and the text sits inside the box rather
	// than across the top of the screen.
	widgets::oled::text(pf, Rect{panel.x, win_y + kTitleDy, panel.w, kTextSpacingY}, d.title,
	                    {.font = pf.style.row_font, .align = widgets::oled::Align::Centre});

	// The name box and the text inside it. `charsStartPixel = (width - 17 * kTextSpacingX) >> 1`,
	// `boxStartPixel = charsStartPixel - 3`, and the box's right edge mirrors its left.
	const int32_t chars_off = (panel.w - kMaxNumChars * kTextSpacingX) >> 1;
	const int32_t chars_x = panel.x + chars_off;
	const int32_t box_off = chars_off - 3;
	widgets::oled::popup_frame(pf, Rect{panel.x + box_off, win_y + kBoxDy, panel.w - 2 * box_off, kBoxH},
	                           widgets::oled::PopupStyle::Permanent);

	// `drawTextForOLEDEditing(charsStartPixel, WIDTH - charsStartPixel + 1, 27, 17, canvas)`: the rect's right edge is
	// legacy's `xPixelMax`, and its width in whole characters is legacy's `maxNumChars`.
	const int32_t chars_w = panel.w - 2 * chars_off + 1;
	widgets::oled::text_edit(pf, Rect{chars_x, win_y + kTextDy, chars_w, kTextSpacingY}, d.text, edit);
}

} // namespace deluge::ui::views::oled
