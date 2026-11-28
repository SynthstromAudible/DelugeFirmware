#pragma once
#include "ui/widgets/core/peaks.h"
#include "ui/widgets/oled/timeline_strips.h"
#include "unit/ui/widgets/support/frames.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

/// @file
/// @brief What the views' specs share: pixel probes over the 48- and 64-row rigs, and the canvas sketches' data.

namespace deluge::ui::views::oled::test {

namespace uitest = deluge::ui::test;
using widgets::oled::LaneNode;
using widgets::oled::LaneSpan;
using widgets::oled::StripNote;

/// The 48-row rig's hidden rows: canvas row = local row + 5.
constexpr int32_t kTop = 5;

/// Lit pixels in the local rect {x, y, w, h} of the 48-row rig's visible area.
inline int32_t lit_in(uitest::Rig48& r, int32_t x, int32_t y, int32_t w, int32_t h) {
	int32_t n = 0;
	for (int32_t yy = y; yy < y + h; yy++) {
		for (int32_t xx = x; xx < x + w; xx++) {
			n += r.lit(xx, yy + kTop) ? 1 : 0;
		}
	}
	return n;
}

/// Lit pixels in the rect {x, y, w, h} of the 64-row rig (no hidden rows).
inline int32_t lit_in(uitest::Rig64& r, int32_t x, int32_t y, int32_t w, int32_t h) {
	int32_t n = 0;
	for (int32_t yy = y; yy < y + h; yy++) {
		for (int32_t xx = x; xx < x + w; xx++) {
			n += r.lit(xx, yy) ? 1 : 0;
		}
	}
	return n;
}

/// Lit pixels on the 48-row rig's visible area.
inline int32_t lit_count(uitest::Rig48& r) {
	return lit_in(r, 0, 0, 128, 43);
}

/// The sketches' clip: 64 steps of 12 ticks, two octaves from C3 (48), a melody over a bass line.
constexpr int32_t kStep = 12;
constexpr int32_t kClipTicks = 64 * kStep;
constexpr uint8_t kLow = 48;
constexpr uint8_t kHigh = 72;
constexpr std::array<StripNote, 24> kClip{{
    {0 * kStep, 2 * kStep, 60},  {2 * kStep, 2 * kStep, 63},  {4 * kStep, 4 * kStep, 67},  {8 * kStep, 2 * kStep, 65},
    {10 * kStep, 2 * kStep, 63}, {12 * kStep, 4 * kStep, 60}, {16 * kStep, 3 * kStep, 58}, {19 * kStep, 1 * kStep, 60},
    {20 * kStep, 4 * kStep, 63}, {24 * kStep, 8 * kStep, 62}, {32 * kStep, 2 * kStep, 60}, {34 * kStep, 2 * kStep, 63},
    {36 * kStep, 4 * kStep, 67}, {40 * kStep, 2 * kStep, 70}, {42 * kStep, 2 * kStep, 72}, {44 * kStep, 4 * kStep, 67},
    {48 * kStep, 4 * kStep, 65}, {52 * kStep, 2 * kStep, 63}, {54 * kStep, 2 * kStep, 62}, {56 * kStep, 8 * kStep, 60},
    {0 * kStep, 8 * kStep, 48},  {16 * kStep, 8 * kStep, 51}, {32 * kStep, 8 * kStep, 53}, {48 * kStep, 8 * kStep, 55},
}};

/// The arrangement of the canvas's "Arranger · idle" sketch: 8 lanes over 64 bars.
constexpr std::array<LaneSpan, 27> kSong{{
    {0, 0, 8, false},  {0, 8, 16, false},  {0, 24, 16, false}, {0, 40, 8, false},  {0, 48, 8, false},
    {1, 8, 16, false}, {1, 24, 16, false}, {1, 44, 12, false}, {2, 4, 4, false},   {2, 8, 8, false},
    {2, 16, 8, false}, {2, 24, 8, false},  {2, 32, 8, false},  {2, 44, 12, false}, {3, 8, 32, false},
    {3, 40, 8, true},  {4, 0, 8, false},   {4, 16, 16, false}, {4, 40, 16, false}, {5, 24, 16, false},
    {5, 44, 8, false}, {6, 16, 8, false},  {6, 32, 8, false},  {7, 6, 2, false},   {7, 22, 2, false},
    {7, 38, 2, false}, {7, 46, 8, true},
}};

/// The sketches' drum break: @p n columns of peaks, hits decaying from columns 4, 20, 36 ... 116 of a 128-column
/// whole, seen @p zoom times closer from column @p offset; valid peaks at +-amp * (1 << 24).
inline std::vector<widgets::core::PeakCol> break_peaks(int32_t n, float zoom = 1.f, float offset = 0.f) {
	constexpr int32_t kHits[] = {4, 20, 36, 44, 52, 68, 84, 92, 100, 116};
	const auto hash = [](int32_t i) {
		const float x = std::sin(static_cast<float>(i) * 127.1f + 311.7f) * 43758.5453f;
		return x - std::floor(x);
	};
	std::vector<widgets::core::PeakCol> cols(static_cast<size_t>(n));
	for (int32_t x = 0; x < n; ++x) {
		const float c = offset + static_cast<float>(x) * 128.f / static_cast<float>(n) / zoom;
		float a = 0.05f;
		for (int32_t k = 0; k < 10; ++k) {
			const float d = c - static_cast<float>(kHits[k]);
			if (d >= 0) {
				a = std::fmax(a, (k % 2 ? 0.7f : 1.f) * std::exp(-d / 7.f));
			}
		}
		a *= 0.6f + 0.4f * hash(static_cast<int32_t>(c * 3));
		const auto v = static_cast<int32_t>(a * static_cast<float>(1 << 24));
		cols[static_cast<size_t>(x)] = {.min24 = -v, .max24 = v, .valid = true};
	}
	return cols;
}

/// The canvas's automation lane over 128 steps of 24 ticks: two swells, the second at half depth, a step up for the
/// last quarter (as `timeline_strips_spec.cpp`'s golden).
inline std::vector<LaneNode> swell_lane() {
	std::vector<LaneNode> nodes;
	for (int32_t i = 0; i < 16; i++) {
		const int32_t x = i * 8;
		const float u = static_cast<float>(x) / 128.f;
		const float swell = (1.f + std::sin(u * 6.28f * 2.f + 0.6f)) * (x < 64 ? 1.f : 0.5f);
		nodes.push_back(
		    {.pos = x * 24, .value = static_cast<int32_t>(swell * 50.f) + (x >= 96 ? 25 : 0), .interpolate = x != 96});
	}
	return nodes;
}

} // namespace deluge::ui::views::oled::test
