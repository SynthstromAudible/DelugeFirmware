#pragma once
#include "ui/gfx/oled/icon.h"
#include "ui/widgets/core/state.h"
#include "ui/widgets/core/style.h"
#include "ui/widgets/oled/frame.h"
#include "ui/widgets/oled/text_edit.h"

#include <cstdint>
#include <span>
#include <string_view>

/// @file
/// @brief The file browser's list: a title, and a folder's rows with the selected one's name being edited.

namespace deluge::ui::views::oled {

/// @brief `Browser::renderOLED`'s two x origins, on the shipped `PartialInversion` profile.
///
/// Legacy: `textStartX = 14`, `iconStartX = 1`, each stepped by one `kTextSpacingX` when
/// `FlashStorage::accessibilityMenuHighlighting == MenuHighlighting::NO_INVERSION`
/// (`Browser::renderOLED` in `gui/ui/browser/browser.cpp`). That setting is what this toolkit spells as
/// `Style::highlighting == Highlighting::NoInversion` -- the same mapping `list`, `columns`, `popup` and `text_edit`
/// already make -- so the view reads the profile rather than the flash byte, and both shipped
/// profiles ship `PartialInversion`, so this stays on the `!= NoInversion` arm.
/// @{
inline constexpr int32_t kBrowserRowTextX = 14;
inline constexpr int32_t kBrowserRowIconX = 1;
/// @}

/// @brief Where a row's name starts under @p style: `kBrowserRowTextX`, stepped by the inset without inversion.
int32_t browser_text_x(const widgets::core::Style& style);

/// @brief One row of the list.
struct BrowserRow {
	std::string_view label; ///< The name as drawn: a folder's whole, a file's before its last dot.
	bool folder;            ///< Drawn with the folder icon rather than the browser's file icon.
	bool selected;          ///< The row the edited name is drawn on, in place of `label`.
	bool scrolls;           ///< The selected name is too wide for the row and side-scrolls, highlighted.
};

/// @brief What the browser's list shows.
struct BrowserList {
	std::string_view title;               ///< The screen's title.
	const gfx::oled::Icon* file_icon;     ///< The icon a file row draws.
	const gfx::oled::Icon* file_icon_pt2; ///< A second icon drawn right of it, or null.
	std::string_view entered;             ///< The name being edited, drawn on the selected row.
	std::span<const BrowserRow> rows;     ///< The rows from the top of the list, at most the style's `menu_rows`.
};

/// @brief `Browser::renderOLED`: the title, then each row's icon and name from `menu_base_y`, `row_height` apart;
///        the selected row's name through `text_edit`, with `scroll_text` over it when it `scrolls`.
/// @param f          The scene's frame.
/// @param d          What to show.
/// @param edit       The text editor's cursor and scroll, updated in place; the caller sets `cursor_lit`.
/// @param row_scroll The selected row's side-scroller, advanced while it scrolls.
void browser_list(widgets::oled::OledFrame& f, const BrowserList& d, widgets::oled::TextEditState& edit,
                  widgets::core::ScrollText& row_scroll);

} // namespace deluge::ui::views::oled
