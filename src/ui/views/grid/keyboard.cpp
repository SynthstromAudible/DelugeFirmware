#include "ui/views/grid/keyboard.h"

#include "ui/gfx/grid/pad_grid.h"
#include "ui/gfx/grid/palette.h"

#include <algorithm>

namespace deluge::ui::views::grid {

widgets::core::Response keyboard(widgets::grid::GridFrame& f, const KeyboardPads& d,
                                 widgets::grid::TextKeyboardState& kb) {
	const widgets::core::Response r = widgets::grid::text_keyboard(f, kb, d.layout, d.home_row);
	if (d.dark) {
		const int32_t cols = std::min<int32_t>(f.bounds().w, gfx::grid::kDelugeGrid.main_cols);
		for (int32_t y = 0; y < f.bounds().h; y++) {
			for (int32_t x = 0; x < cols; x++) {
				(void)f.pad(x, y, deluge::gui::colours::black);
			}
		}
	}
	return r;
}

} // namespace deluge::ui::views::grid
