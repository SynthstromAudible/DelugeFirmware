/*
 * Copyright © 2026 Sean Ditny
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <cstdint>

namespace deluge::gui::session_layout::hybrid {
constexpr int32_t kColumns = 8;
constexpr int32_t kTracks = 7;

constexpr int32_t trackRow(int32_t track, int32_t scroll) {
	int32_t visible = track - scroll;
	return visible >= 0 && visible < kTracks ? kTracks - visible : -1;
}

constexpr int32_t trackIndex(int32_t row, int32_t scroll) {
	return row > 0 && row <= kTracks ? kTracks - row + scroll : -1;
}

constexpr int32_t sectionColumn(int32_t section, int32_t scroll, int32_t count) {
	int32_t column = section - scroll;
	return section >= 0 && section < count && column >= 0 && column < kColumns ? column : -1;
}

constexpr int32_t sectionIndex(int32_t column, int32_t scroll, int32_t count) {
	int32_t section = column + scroll;
	return column >= 0 && column < kColumns && section >= 0 && section < count ? section : -1;
}

constexpr int32_t previewTick(int32_t rowViewTick) {
	return rowViewTick >= 0 && rowViewTick < kColumns * 2 ? kColumns + rowViewTick / 2 : 255;
}
} // namespace deluge::gui::session_layout::hybrid
