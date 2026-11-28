#pragma once
/// @file
/// @brief A horizontal scroll sliding the main pads one column at a time from what they showed to the new view:
///        legacy's `UI_MODE_HORIZONTAL_SCROLL`, as `PadLEDs::horizontal::renderScroll` draws it.

#include "ui/gfx/grid/pad_grid.h"
#include "ui/widgets/grid/frame.h"

#include <cstdint>
#include <optional>

namespace deluge::ui::widgets::grid {

/// @brief How often a running scroll moves one more column, in ms: `UI_MS_PER_REFRESH_SCROLLING` (`board_config.h`).
inline constexpr uint32_t kScrollStepMs = 7;

/// @brief A horizontal scroll sliding between two frames of the same columns.
///
/// The owner starts it with `begin_scroll` from what it showed and paints the new view into the `to` it answers; each
/// render then hands its frame to `scroll_transition`, which paints the step and answers whether the scroll is still
/// running. While it runs the owner paints nothing of its own over those columns.
struct ScrollTransition {
	std::optional<gfx::grid::PadGrid> from; ///< what was on the pads when the view moved
	std::optional<gfx::grid::PadGrid> to;   ///< the new view, as `from`
	int8_t direction = 1;                   ///< +1: the view moved right, the pads slide left; -1 the other way
	uint8_t columns = 0;                    ///< how many columns slide (legacy's `areaToScroll`)
	uint32_t start_ms = 0;                  ///< when the first step was drawn
	bool active = false;                    ///< whether it is still sliding
};

/// @brief Start a scroll from @p from at @p now_ms (`TimelineView::initiateXScroll`).
/// @param s The transition to (re)start; a running one is replaced.
/// @param from What the pads showed; its columns all slide.
/// @param direction +1 when the view moved right, -1 left.
/// @param now_ms The clock now.
/// @return The scroll's `to`: black at `from`'s dimensions, for the caller to paint the new view into.
gfx::grid::PadGrid& begin_scroll(ScrollTransition& s, const gfx::grid::PadGrid& from, int8_t direction,
                                 uint32_t now_ms);

/// @brief Paint the running scroll's step for @p f's clock, as `renderScroll` does: the first step at once, one
///        column more every `kScrollStepMs`, until every column has come in.
/// @param f The frame over the sliding columns.
/// @param s The transition.
/// @return True while the scroll runs (this frame painted one of its steps); false once it has ended, when the owner
///         paints the view itself.
bool scroll_transition(GridFrame& f, ScrollTransition& s);

} // namespace deluge::ui::widgets::grid
