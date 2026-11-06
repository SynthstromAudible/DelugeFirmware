#include "ui/grid/zoom_transition.h"

#include <algorithm>
#include <cstdint>

namespace deluge::ui::grid {

gfx::grid::PadGrid& begin_zoom(ZoomTransition& z, const gfx::grid::PadGrid& from, gfx::grid::ZoomParams params,
                               uint32_t now_ms) {
	z.from.emplace(from);
	z.to.emplace(from.dims());
	z.out.emplace(from.dims());
	z.params = params;
	z.start_ms = now_ms;
	z.active = true;
	return *z.to;
}

bool zoom_transition(GridFrame& f, ZoomTransition& z) {
	if (!z.active || !z.from.has_value() || !z.to.has_value() || !z.out.has_value()) {
		return false;
	}
	const uint32_t now = f.now_ms();
	const uint64_t progress = (static_cast<uint64_t>(now - z.start_ms) * 65536u) / z.length_ms;
	if (progress >= 65536u) {
		// `renderZoom`'s end: the mode exits and the view renders itself again.
		z.active = false;
		return false;
	}
	z.params.t = static_cast<uint16_t>(progress);
	gfx::grid::zoom(*z.from, *z.to, z.params, *z.out);

	const gfx::grid::GridDimensions d = z.out->dims();
	const int32_t w = std::min<int32_t>(f.bounds().w, d.cols());
	const int32_t h = std::min<int32_t>(f.bounds().h, d.rows);
	for (int32_t y = 0; y < h; y++) {
		for (int32_t x = 0; x < w; x++) {
			(void)f.pad(x, y, z.out->at(static_cast<uint8_t>(x), static_cast<uint8_t>(y)));
		}
	}
	f.grid_gate().request_repaint_after(kZoomStepMs, now);
	return true;
}

} // namespace deluge::ui::grid
