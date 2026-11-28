#include "ui/views/grid/blink.h"

namespace deluge::ui::views::grid {

void blink(widgets::grid::GridFrame& f, const BlinkPads& d) {
	if (d.fire) {
		widgets::grid::fire_blink_step(f, d.plan, d.step);
	}
}

} // namespace deluge::ui::views::grid
