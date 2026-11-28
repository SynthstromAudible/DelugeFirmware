#include "ui/widgets/grid/scroll_transition.h"

#include <algorithm>
#include <cstdint>

namespace deluge::ui::widgets::grid {

gfx::grid::PadGrid& begin_scroll(ScrollTransition& s, const gfx::grid::PadGrid& from, int8_t direction,
                                 uint32_t now_ms) {
	s.from.emplace(from);
	s.to.emplace(from.dims());
	s.direction = direction >= 0 ? 1 : -1;
	s.columns = static_cast<uint8_t>(from.dims().cols());
	s.start_ms = now_ms;
	s.active = true;
	return *s.to;
}

bool scroll_transition(GridFrame& f, ScrollTransition& s) {
	if (!s.active || !s.from.has_value() || !s.to.has_value() || s.columns == 0) {
		return false;
	}
	const uint32_t now = f.now_ms();
	// The first step is drawn as the scroll starts, then one a refresh.
	const uint32_t steps = 1 + (now - s.start_ms) / kScrollStepMs;
	if (steps >= s.columns) {
		// `renderScroll`'s last step ends the mode, and the view renders itself again.
		s.active = false;
		return false;
	}
	const auto k = static_cast<int32_t>(steps);
	const int32_t n = s.columns;
	const int32_t w = std::min<int32_t>(f.bounds().w, n);
	const int32_t h = std::min<int32_t>(f.bounds().h, s.from->dims().rows);
	for (int32_t y = 0; y < h; y++) {
		for (int32_t x = 0; x < w; x++) {
			// Moving right, each step shifts the pads one column left and brings the new view's next column in at
			// the right edge; moving left, the mirror image.
			const int32_t src = s.direction > 0 ? x + k : x - k;
			const bool old = src >= 0 && src < n;
			const int32_t col = old ? src : (s.direction > 0 ? src - n : src + n);
			const gfx::grid::PadGrid& g = old ? *s.from : *s.to;
			(void)f.pad(x, y, g.at(static_cast<uint8_t>(col), static_cast<uint8_t>(y)));
		}
	}
	f.grid_gate().request_repaint_after(kScrollStepMs, now);
	return true;
}

} // namespace deluge::ui::widgets::grid
