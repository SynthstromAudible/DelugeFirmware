#pragma once
#include "gfx/grid/pad_grid.h"

namespace deluge::gfx::grid {
// Pure frame→frame transitions. The caller owns timing; `out` must share `dims()` with the inputs.

/// Blend `from` toward `to` at parameter `t` (0..65535: 0 = from, 65535 = to). Leaves out.dirty_rows()
/// fully set.
void fade(const PadGrid& from, const PadGrid& to, uint16_t t, PadGrid& out);

/// Shift `from` by `offset` columns (positive = content moves right); `incoming` fills the
/// vacated columns from its far edge. Sidebar columns are untouched. All rows dirty.
void scroll_horizontal(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);

/// Same for rows (positive = content moves down).
void scroll_vertical(const PadGrid& from, const PadGrid& incoming, int32_t offset, PadGrid& out);

/// RGB::greyOut applied to every main-grid pad; proportion as in RGB::greyOut (a ~0..6500000
/// scale for a full grey-out, NOT the 0..65535 scale `fade`'s `t` and `RGB::blend`'s index use --
/// see existing callers in pad_leds.cpp/session_view.cpp/load_song_ui.cpp). Sidebar pads pass
/// through untouched.
void grey_out(const PadGrid& in, int32_t proportion, PadGrid& out);

} // namespace deluge::gfx::grid
