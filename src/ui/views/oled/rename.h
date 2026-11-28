#pragma once
#include "ui/widgets/oled/frame.h"
#include "ui/widgets/oled/text_edit.h"

#include <string_view>

/// @file
/// @brief The rename window: a title, and the name being typed in a box.

namespace deluge::ui::views::oled {

/// @brief What the rename window shows.
struct RenameWindow {
	std::string_view title; ///< What is being renamed, centred in the window's top.
	std::string_view text;  ///< The name typed so far.
};

/// @brief `RenameUI::renderOLED`: a 120x40 window centred on the whole panel, the title, and the name box with
///        `text_edit` inside it.
/// @param f    The scene's frame; the window is drawn on `panel_frame(f)`.
/// @param d    What to show.
/// @param edit The text editor's cursor and scroll, updated in place; the caller sets `cursor_lit`.
void rename_window(widgets::oled::OledFrame& f, const RenameWindow& d, widgets::oled::TextEditState& edit);

} // namespace deluge::ui::views::oled
