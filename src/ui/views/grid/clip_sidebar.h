#pragma once
#include "ui/gfx/grid/pad_grid.h"
#include "ui/widgets/grid/columns.h"
#include "ui/widgets/grid/frame.h"

#include <array>
#include <cstdint>

/// @file
/// @brief A clip view's sidebar: both columns black, with the session macros in the first while SESSION is held.

namespace deluge::ui::views::grid {

/// @brief What the sidebar shows this pass.
struct ClipSidebar {
	bool macros; ///< SESSION is held: the macro column goes over the first sidebar column.
	std::array<widgets::grid::MacroCell, gfx::grid::kDelugeGrid.rows> cells; ///< Each row's macro, top-down.
};

/// @brief `AudioClipView::renderSidebar`: `sidebar_columns` black, then `macros(f, main_cols, d.cells, -1)` when
///        `d.macros`.
/// @param f         The root frame.
/// @param main_cols The first sidebar column.
/// @param d         What to show.
void clip_sidebar(widgets::grid::GridFrame& f, int32_t main_cols, const ClipSidebar& d);

} // namespace deluge::ui::views::grid
