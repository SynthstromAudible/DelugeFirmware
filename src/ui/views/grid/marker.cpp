#include "ui/views/grid/marker.h"

#include <algorithm>

namespace deluge::ui::views::grid {

void marker_overlay(widgets::grid::GridFrame& main, const MarkerOverlay& d) {
	const int32_t cols = std::min<int32_t>(main.bounds().w, static_cast<int32_t>(d.active.size()));
	for (int32_t col = 0; col < cols; col++) {
		if (col == d.flood) {
			widgets::grid::marker_tint(main, col, 0, main.bounds().h, d.colours[static_cast<size_t>(d.selected)],
			                           false);
			continue;
		}
		const uint32_t active = d.active[static_cast<size_t>(col)];
		if (active == 0) {
			continue;
		}
		widgets::grid::marker_column(
		    main, col, widgets::grid::MarkerStack{.active = active, .suppress = d.suppress, .selected = d.selected},
		    d.colours);
	}
}

} // namespace deluge::ui::views::grid
