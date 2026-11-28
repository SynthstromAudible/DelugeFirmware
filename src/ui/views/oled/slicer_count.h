#pragma once
#include "ui/widgets/oled/frame.h"

#include <string_view>

/// @file
/// @brief The slicer's screen: the label and the number of slices, in a window.

namespace deluge::ui::views::oled {

/// @brief What the slicer's screen shows.
struct SlicerCount {
	std::string_view label;  ///< The heading over the count.
	std::string_view digits; ///< The count, already formatted.
};

/// @brief `Slicer::renderOLED`: `count_window` over the label and the count.
void slicer_count(widgets::oled::OledFrame& f, const SlicerCount& d);

} // namespace deluge::ui::views::oled
