#include "ui/views/grid/playhead.h"

namespace deluge::ui::views::grid {

void playhead(widgets::grid::GridFrame& f, const Playhead& d, std::span<uint8_t> last) {
	if (d.paint) {
		widgets::grid::tick_squares(f, d.per_row, d.muted);
	}
	if (d.flash) {
		widgets::grid::flash_tick_squares(f, d.per_row, last, d.fast);
	}
}

} // namespace deluge::ui::views::grid
