#pragma once
#include "ui/widgets/oled/timeline_strips.h"
#include "unit/ui/widgets/support/frames.h"

#include <array>
#include <cstdint>

/// @file
/// @brief What the views' specs share: pixel probes over the 48- and 64-row rigs, and the canvas sketches' data.

namespace deluge::ui::views::oled::test {

namespace uitest = deluge::ui::test;
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

} // namespace deluge::ui::views::oled::test
