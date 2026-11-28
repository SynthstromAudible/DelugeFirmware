#include "ui/views/grid/scroll_slide.h"

namespace deluge::ui::views::grid {

bool scroll_slide(widgets::grid::GridFrame& main, widgets::grid::ScrollTransition& s) {
	return widgets::grid::scroll_transition(main, s);
}

} // namespace deluge::ui::views::grid
