#include "ui/views/grid/zoom_slide.h"

namespace deluge::ui::views::grid {

bool zoom_slide(widgets::grid::GridFrame& main, widgets::grid::ZoomTransition& z) {
	return widgets::grid::zoom_transition(main, z);
}

} // namespace deluge::ui::views::grid
