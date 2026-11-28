#include "ui/views/oled/browser.h"

#include "ui/gfx/oled/geometry.h"
#include "ui/gfx/oled/icons.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/oled/misc.h"
#include "ui/widgets/oled/scroll_text.h"
#include "ui/widgets/oled/text.h"

namespace deluge::ui::views::oled {

namespace {

using gfx::oled::Rect;
using widgets::core::Highlighting;
using widgets::core::kTextSpacingY;

} // namespace

int32_t browser_text_x(const widgets::core::Style& style) {
	return kBrowserRowTextX + (style.highlighting != Highlighting::NoInversion ? 0 : style.inset_x);
}

void browser_list(widgets::oled::OledFrame& f, const BrowserList& d, widgets::oled::TextEditState& edit,
                  widgets::core::ScrollText& row_scroll) {
	// `canvas.drawScreenTitle(title)` (`Browser::renderOLED` in `gui/ui/browser/browser.cpp`).
	widgets::oled::title(f, d.title);

	// `gui/ui/browser/browser.cpp`. The `NO_INVERSION` step is `Style::highlighting == Highlighting::
	// NoInversion` here -- the same mapping `list`, `columns`, `popup` and `text_edit` already
	// make for the same flash setting.
	const int32_t icon_x = f.style.highlighting != Highlighting::NoInversion ? kBrowserRowIconX : f.style.inset_x;
	const int32_t text_x = browser_text_x(f.style);
	const int32_t width = f.bounds().w;

	int32_t y = f.style.menu_base_y;
	for (const BrowserRow& row : d.rows) {
		// `drawGraphicMultiLine(graphic, iconStartX, yPixel, iconWidth)` (`Browser::renderOLED`).
		const gfx::oled::Icon& glyph = row.folder ? gfx::oled::icons::browserFolder : *d.file_icon;
		widgets::oled::icon(f, gfx::oled::Point{icon_x, y}, glyph);
		if (!row.folder && d.file_icon_pt2 != nullptr) {
			widgets::oled::icon(f, gfx::oled::Point{icon_x + glyph.width, y}, *d.file_icon_pt2);
		}

		const Rect rect{text_x, y, width - text_x, kTextSpacingY};
		if (row.selected) {
			// `drawTextForOLEDEditing(textStartX, OLED_MAIN_WIDTH_PIXELS, yPixel, maxChars, canvas)`
			// (`qwerty_ui.cpp`): the rect's right edge is legacy's `xPixelMax` and its width in whole
			// characters is legacy's `maxChars`.
			widgets::oled::text_edit(f, rect, d.entered, edit);
			// A name too wide for the row side-scrolls, highlighted, over what was just drawn
			// (`OLED::setupSideScroller`, which legacy arms only for a name that does not fit).
			if (row.scrolls) {
				(void)widgets::oled::scroll_text(f, rect, d.entered, f.style.row_font, row_scroll, true, true);
			}
		}
		else {
			// `canvas.drawString(name[0..displayStringLength), textStartX, yPixel, ...)` (`Browser::renderOLED`).
			widgets::oled::text(f, rect, row.label, {.font = f.style.row_font});
		}
		y += f.style.row_height;
	}
}

} // namespace deluge::ui::views::oled
