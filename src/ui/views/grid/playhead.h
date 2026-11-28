#pragma once
#include "ui/gfx/grid/pad_grid.h"
#include "ui/gfx/grid/rgb.h"
#include "ui/widgets/grid/columns.h"
#include "ui/widgets/grid/frame.h"

#include <array>
#include <cstdint>
#include <span>

/// @file
/// @brief A clip's playhead on the pads: each row's tick square, painted or flashed as the flash cursor says.

namespace deluge::ui::views::grid {

/// @brief What the playhead shows on one pass.
struct Playhead {
	std::array<widgets::grid::TickSquare, gfx::grid::kDelugeGrid.rows> per_row; ///< Each row's square, top-down.
	bool paint;                                                                 ///< Paint the squares (cursor Slow).
	RGB muted;  ///< A muted square's colour, where `paint`.
	bool flash; ///< Move the flash latch, flashing where `fast` (cursor Slow or Fast).
	bool fast;  ///< The PIC flashes each moved square (cursor Fast).
};

/// @brief `tick_squares(f, d.per_row, d.muted)` when `d.paint`, then `flash_tick_squares(f, d.per_row, last, d.fast)`
///        when `d.flash`.
/// @param f    The root frame, painted already: the squares go over it.
/// @param d    What to show.
/// @param last The flash latch, each row's square at the last flash: the scene's, written here when `d.flash`.
void playhead(widgets::grid::GridFrame& f, const Playhead& d, std::span<uint8_t> last);

} // namespace deluge::ui::views::grid
