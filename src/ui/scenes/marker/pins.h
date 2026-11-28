#pragma once

/// @file
/// @brief What `marker/marker_scene.cpp` and `marker/present.cpp` share about the markers' pins: a marker's index and a
/// pin's
///        column for comparisons.

#include "project/sample_marker.h"
#include "ui/views/waveform/view.h"

#include <cstdint>
#include <limits>
#include <utility>

namespace deluge::ui::scenes::marker {

/// @brief The marker's index in `project::SampleMarker` order, 0 to 3.
[[nodiscard]] constexpr int32_t idx(project::SampleMarker t) {
	return std::to_underlying(t);
}

/// @brief A pin's column for the comparisons that place and refuse markers: an unset marker compares as left of every
///        column, so every `column <= x` test refuses on it and every `column >= x` test passes it by.
[[nodiscard]] constexpr int64_t column_or_far_left(const views::waveform::MarkerPin& p) {
	return p.column.value_or(std::numeric_limits<int64_t>::min());
}

} // namespace deluge::ui::scenes::marker
