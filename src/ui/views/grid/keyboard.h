#pragma once
#include "ui/widgets/core/response.h"
#include "ui/widgets/grid/frame.h"
#include "ui/widgets/grid/keyboard.h"

#include <cstdint>

/// @file
/// @brief The on-pad text keyboard, drawn or only listening.

namespace deluge::ui::views::grid {

/// @brief What the text keyboard shows.
struct KeyboardPads {
	widgets::grid::KeyboardLayoutKind layout; ///< Which layout's characters the keys type.
	int32_t home_row;                         ///< The row the keyboard is centred on.
	bool dark; ///< Every main pad is painted black: the keys still take presses, a screen shows what they type.
};

/// @brief `QwertyUI::drawKeys`: `text_keyboard`, then, when `d.dark`, every main pad black.
/// @param f  A frame over the pads; only the main pads are painted, the sidebar is left alone.
/// @param d  What to show.
/// @param kb The keyboard's held-key state, updated in place.
/// @return `text_keyboard`'s response: the typed character in `index`, the held one in `held_index`.
widgets::core::Response keyboard(widgets::grid::GridFrame& f, const KeyboardPads& d,
                                 widgets::grid::TextKeyboardState& kb);

} // namespace deluge::ui::views::grid
