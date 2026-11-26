#include "ui/widgets/oled/waveform.h"

#include "ui/gfx/oled/icons.h"

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

void loop_marker(OledFrame& f, int32_t line_x, int32_t y0, int32_t y1, int32_t icon_y, bool start) {
	for (int32_t y = y0; y <= y1; y += 2) {
		f.pixel(line_x, y);
	}
	const gfx::oled::Icon& icon = gfx::oled::icons::loopPoint;
	const int32_t icon_x = start ? line_x + 4 : line_x - 3 - icon.width;
	f.icon(icon, icon_x, icon_y, !start);
}

void looped_waveform(OledFrame& f, gfx::oled::Rect r, std::span<const core::PeakCol> cols, int32_t start_col,
                     int32_t end_col) {
	constexpr int32_t kIconBand = 9; // the icon's 8 rows and one clear
	waveform(f, {r.x, r.y + kIconBand, r.w, r.h - kIconBand}, cols);
	loop_marker(f, r.x + start_col, r.y, r.y + r.h - 1, r.y, true);
	loop_marker(f, r.x + end_col, r.y, r.y + r.h - 1, r.y, false);
}

} // namespace deluge::ui::widgets::oled
