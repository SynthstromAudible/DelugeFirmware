#include "ui/widgets/oled/fx_graphs.h"

#include "ui/widgets/oled/page_drawing.h"

#include <algorithm>
#include <cmath>

namespace deluge::ui::widgets::oled {

namespace {

/// @brief 0 at @p t = 0, 1 at 1, bent by @p k octaves; 0 is a straight line. The envelope page's stage bend.
float rising(float k, float t) {
	if (k == 0.f) {
		return t;
	}
	const float floor = std::exp2(-k);
	return 1.f - ((std::exp2(-k * t) - floor) / (1.f - floor));
}

int32_t round_i(float v) {
	return static_cast<int32_t>(std::lround(v));
}

/// The duck's recovery, as `curve` reads it.
struct Recovery {
	float peak, rest, k;
	[[nodiscard]] float at(float t) const { return peak + (rest - peak) * rising(k, t); }
};

} // namespace

void duck_graph(OledFrame& panel, const DuckGraph& d, DuckHandles h, int32_t focused, float phase) {
	const PageBody b = page_body(panel);
	const float volume = std::clamp(d.volume, -1.f, 1.f);
	const float shape = std::clamp(d.shape, 0.f, 1.f);
	const float attack = std::clamp(d.attack, 0.f, 1.f);
	const float release = std::clamp(d.release, 0.f, 1.f);

	const float depth = std::abs(volume);
	const Recovery rec{
	    .peak = volume > 0.f ? 1.f : 1.f - depth, .rest = volume > 0.f ? 1.f - depth : 1.f, .k = 4.f * shape};
	const int32_t xt = b.left + 4;
	const int32_t xa = xt + round_i(attack * static_cast<float>(b.right - b.left) / 4.f);
	const int32_t xr = xa + round_i(release * static_cast<float>(b.right - xa - 4));
	const int32_t y_rest = b.y_at(rec.rest);
	const int32_t y_peak = b.y_at(rec.peak);

	guide_v(panel, b, xt, b.top);
	guide_h(panel, y_peak, b.left, xa);
	panel.hline(y_rest, b.left, xt);
	panel.line(xt, y_rest, xa, y_peak);
	curve(
	    panel, b, static_cast<float>(xa), static_cast<float>(xr),
	    [](float t, const void* ctx) { return static_cast<const Recovery*>(ctx)->at(t); }, &rec);
	panel.hline(y_rest, xr, b.right);

	if (phase >= 0.f) {
		const int32_t x = b.left + round_i(std::clamp(phase, 0.f, 1.f) * static_cast<float>(b.right - b.left));
		int32_t y = y_rest;
		if (x >= xt && x < xa) {
			y = y_rest + (y_peak - y_rest) * (x - xt) / std::max(1, xa - xt);
		}
		else if (x >= xa && x <= xr) {
			y = b.y_at(rec.at(xr > xa ? static_cast<float>(x - xa) / static_cast<float>(xr - xa) : 1.f));
		}
		for (int32_t dy = -1; dy <= 1; dy++) {
			panel.hline(y + dy, x - 1, x + 1);
		}
	}

	Handles marks{panel};
	const auto mark = [&](int8_t column, int32_t x, int32_t y) {
		if (column >= 0) {
			marks.draw(x, y, column == focused);
		}
	};
	// The focused one last, so an unfocused handle at the same spot cannot draw over it.
	const auto shape_x = round_i(static_cast<float>(xa) + static_cast<float>(xr - xa) * 0.5f);
	const auto shape_y = b.y_at(rec.at(0.5f));
	struct Mark {
		int8_t column;
		int32_t x, y;
	};
	const Mark all[] = {
	    {h.volume, b.left + 1, y_peak}, {h.attack, xa, y_peak}, {h.shape, shape_x, shape_y}, {h.release, xr, y_rest}};
	for (const Mark& m : all) {
		if (m.column != focused) {
			mark(m.column, m.x, m.y);
		}
	}
	for (const Mark& m : all) {
		if (m.column == focused) {
			mark(m.column, m.x, m.y);
		}
	}
}

} // namespace deluge::ui::widgets::oled
