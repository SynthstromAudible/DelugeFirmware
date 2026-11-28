#include "ui/widgets/oled/fx_graphs.h"

#include "ui/widgets/oled/page_drawing.h"

#include <algorithm>
#include <cmath>
#include <numbers>

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

namespace {

/// The distortion's output row at column @p x, per `distortion_graph`'s doc.
struct Distortion {
	const PageBody& b;
	float saturation, bitcrush, wavefold;
	int32_t hold, cy, amp;

	[[nodiscard]] float u(int32_t x) const {
		return static_cast<float>(x - b.left) / static_cast<float>(b.right - b.left);
	}
	[[nodiscard]] int32_t clean_row(int32_t x) const {
		return cy - round_i(static_cast<float>(amp) * std::sin(2.f * std::numbers::pi_v<float> * u(x)));
	}
	[[nodiscard]] int32_t row(int32_t x) const {
		const int32_t held = b.left + ((x - b.left) / hold) * hold;
		float v = std::sin(2.f * std::numbers::pi_v<float> * u(held));
		const float g = 1.f + 4.f * saturation;
		v = std::tanh(g * v) / std::tanh(g);
		const float t = 1.f - 0.6f * wavefold;
		if (std::abs(v) > t) {
			v = (v > 0.f ? 1.f : -1.f) * (2.f * t - std::abs(v));
		}
		if (bitcrush > 0.f) {
			const float n = std::round(2.f + (1.f - bitcrush) * 14.f);
			v = std::round(v * n) / n;
		}
		return cy - round_i(static_cast<float>(amp) * v);
	}
};

} // namespace

void distortion_graph(OledFrame& panel, const DistortionGraph& d, int32_t focused) {
	const PageBody b = page_body(panel);
	const Distortion dist{.b = b,
	                      .saturation = std::clamp(d.saturation, 0.f, 1.f),
	                      .bitcrush = std::clamp(d.bitcrush, 0.f, 1.f),
	                      .wavefold = std::clamp(d.wavefold, 0.f, 1.f),
	                      .hold = 1 + round_i(std::clamp(d.decimation, 0.f, 1.f) * 10.f),
	                      .cy = b.center_y(),
	                      .amp = b.height() / 2 - 1};

	for (int32_t x = b.left + (b.left & 1); x <= b.right; x += 2) { // even columns
		panel.pixel(x, dist.clean_row(x));
	}
	int32_t prev = dist.row(b.left);
	panel.pixel(b.left, prev);
	for (int32_t x = b.left + 1; x <= b.right; x++) {
		const int32_t y = dist.row(x);
		panel.pixel(x, prev);
		if (y != prev) {
			panel.vline(x, std::min(prev, y), std::max(prev, y));
		}
		prev = y;
	}

	Handles marks{panel};
	const float us[] = {0.25f, 0.10f, 0.58f, 0.75f};
	const auto x_at = [&](float u) { return b.left + round_i(u * static_cast<float>(b.right - b.left)); };
	for (int32_t i = 0; i < 4; i++) {
		if (i != focused) {
			marks.draw(x_at(us[i]), dist.row(x_at(us[i])), false);
		}
	}
	if (focused >= 0 && focused < 4) {
		marks.draw(x_at(us[focused]), dist.row(x_at(us[focused])), true);
	}
}

void swing_graph(OledFrame& panel, int32_t amount, int32_t focused) {
	const PageBody b = page_body(panel);
	// room for the last step pushed a whole pitch late, 2 px wide, inside the body
	const int32_t pitch = (b.right - b.left - 3) / 8;
	const int32_t late =
	    round_i(static_cast<float>(std::clamp<int32_t>(amount, 1, 99) - 50) / 50.f * static_cast<float>(pitch));
	const int32_t top = b.top + 5;
	const auto xs = [&](int32_t i) { return b.left + 2 + i * pitch; };
	const auto x_step = [&](int32_t i) { return xs(i) + (i % 2 == 1 ? late : 0); };
	for (int32_t i = 0; i < 8; i++) {
		guide_v(panel, b, xs(i), b.top);
	}
	for (int32_t i = 0; i < 8; i++) {
		panel.vline(x_step(i), top, b.bottom);
		panel.vline(x_step(i) + 1, top, b.bottom);
	}
	Handles marks{panel};
	const int32_t hx[] = {x_step(1), xs(2)};
	for (int32_t i = 0; i < 2; i++) {
		if (i != focused) {
			marks.draw(hx[i], top, false);
		}
	}
	if (focused == 0 || focused == 1) {
		marks.draw(hx[focused], top, true);
	}
}

} // namespace deluge::ui::widgets::oled
