#include "ui/views/grid/waveform.h"

#include <algorithm>

namespace deluge::ui::views::grid {

void waveform(widgets::grid::GridFrame& f, std::span<const widgets::grid::PeakCol> cols, int32_t brightness,
              std::optional<RGB> colour) {
	const int32_t w = std::min<int32_t>(f.bounds().w, static_cast<int32_t>(cols.size()));
	f.region(gfx::grid::Rect{0, 0, w, f.bounds().h},
	         [&](widgets::grid::GridFrame& main) { widgets::grid::waveform_full(main, cols, brightness, colour); });
}

} // namespace deluge::ui::views::grid
