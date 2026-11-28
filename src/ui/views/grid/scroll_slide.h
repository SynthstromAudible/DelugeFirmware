#pragma once
#include "ui/widgets/grid/frame.h"
#include "ui/widgets/grid/scroll_transition.h"

/// @file
/// @brief A horizontal scroll's slide over the main pads.

namespace deluge::ui::views::grid {

/// @brief The running slide's step for @p main's clock (`scroll_transition(main, s)`, `PadLEDs::renderScroll`).
/// @param main A region over the main pads, whose local corner is the slide's (0, 0).
/// @param s    The slide, started by the scene with `begin_scroll`; ended here once its time runs out.
/// @return Whether the slide is still running (and painted): while it is, the scene paints nothing of its own over it.
bool scroll_slide(widgets::grid::GridFrame& main, widgets::grid::ScrollTransition& s);

} // namespace deluge::ui::views::grid
