#pragma once
#include "ui/widgets/grid/frame.h"
#include "ui/widgets/grid/shortcut_blink.h"

#include <cstdint>

/// @file
/// @brief The shortcut blink as pads: the flash a counter step plays.

namespace deluge::ui::views::grid {

/// @brief What a scene's blinking pads show on one pass.
struct BlinkPads {
	widgets::grid::BlinkPlan plan; ///< Which pads blink.
	uint32_t step;                 ///< The blink counter's value.
	bool fire;                     ///< Whether this step has not played yet.
};

/// @brief `fire_blink_step(f, d.plan, d.step)` when `d.fire`; a step already played flashes nothing.
void blink(widgets::grid::GridFrame& f, const BlinkPads& d);

} // namespace deluge::ui::views::grid
