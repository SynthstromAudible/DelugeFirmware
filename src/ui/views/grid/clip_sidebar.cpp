#include "ui/views/grid/clip_sidebar.h"

#include "ui/widgets/grid/sidebar.h"

namespace deluge::ui::views::grid {

void clip_sidebar(widgets::grid::GridFrame& f, int32_t main_cols, const ClipSidebar& d) {
	const std::array<RGB, gfx::grid::kDelugeGrid.rows> black{};
	widgets::grid::sidebar_columns(f, black, black);
	if (d.macros) {
		widgets::grid::macros(f, main_cols, d.cells, -1);
	}
}

} // namespace deluge::ui::views::grid
