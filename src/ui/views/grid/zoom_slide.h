#pragma once
#include "ui/widgets/grid/frame.h"
#include "ui/widgets/grid/zoom_transition.h"

/// @file
/// @brief A horizontal zoom's slide over the main pads.

namespace deluge::ui::views::grid {

/// @brief The running slide's step for @p main's clock (`zoom_transition(main, z)`, `PadLEDs::renderZoom`).
/// @param main A region over the main pads, whose local corner is the slide's (0, 0).
/// @param z    The slide, started by the scene with `begin_zoom`; ended here once its time runs out.
/// @return Whether the slide is still running (and painted): while it is, the scene paints nothing of its own over it.
bool zoom_slide(widgets::grid::GridFrame& main, widgets::grid::ZoomTransition& z);

} // namespace deluge::ui::views::grid
