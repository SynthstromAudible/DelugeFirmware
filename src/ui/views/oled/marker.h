#pragma once
#include "ui/widgets/oled/frame.h"

#include <cstdint>
#include <string_view>

/// @file
/// @brief The sample marker editor's screen: the selected marker's name, the loop lock, and where the marker sits.

namespace deluge::ui::views::oled {

/// @brief What the marker editor's screen shows.
struct MarkerPosition {
	std::string_view label; ///< The marker's name, the title.
	bool loop_locked;       ///< The loop is locked: the lock badge on the title.
	uint64_t position;      ///< The marker's position, in samples.
	uint32_t sample_rate;   ///< The sample's rate, for the duration line.
};

/// @brief `SampleMarkerEditor::renderOLED`: `title`, the lock as a `title_badge` when locked, and `sample_position`'s
///        two lines at `{1, style.title_height + 5, w - 1, 2 * kSamplePositionLineGap}`.
void marker_position(widgets::oled::OledFrame& f, const MarkerPosition& d);

} // namespace deluge::ui::views::oled
