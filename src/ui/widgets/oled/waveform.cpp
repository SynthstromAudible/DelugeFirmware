#include "ui/widgets/oled/waveform.h"

#include <algorithm>

namespace deluge::ui::widgets::oled {

namespace {

/// @brief The row of a peak value in @p r: `1 << 24` is @p half rows above @p centre, clamped to @p r.
int32_t peak_row(gfx::oled::Rect r, int32_t centre, int32_t half, int32_t v) {
	const auto offset = static_cast<int32_t>((static_cast<int64_t>(v) * half) >> 24);
	return std::clamp(centre - offset, r.y, r.y + r.h - 1);
}

} // namespace

void waveform(OledFrame& f, gfx::oled::Rect r, std::span<const core::PeakCol> cols) {
	const int32_t half = (r.h - 1) / 2;
	const int32_t centre = r.y + half;
	const auto n = std::min(static_cast<int32_t>(cols.size()), r.w);
	for (int32_t i = 0; i < n; i++) {
		const core::PeakCol& c = cols[static_cast<size_t>(i)];
		const int32_t x = r.x + i;
		if (!c.valid) {
			if ((i & 1) == 0) {
				f.pixel(x, centre);
			}
			continue;
		}
		f.vline(x, peak_row(r, centre, half, c.max24), peak_row(r, centre, half, c.min24));
	}
}

} // namespace deluge::ui::widgets::oled
