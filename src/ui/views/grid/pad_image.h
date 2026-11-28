#pragma once
#include "ui/gfx/grid/pad_grid.h"
#include "ui/gfx/grid/rgb.h"
#include "ui/widgets/grid/frame.h"

#include <cstdint>

/// @file
/// @brief A snapshot of the main pads, painted back as it was taken.

namespace deluge::ui::views::grid {

/// @brief What the 16x8 main pads showed, pad for pad: `pads[y][x]`.
struct PadImage {
	RGB pads[gfx::grid::kDelugeGrid.rows][gfx::grid::kDelugeGrid.main_cols]; ///< Each main pad's colour.
};

/// @brief Paint @p d over @p f's main pads: every pad both @p d and @p f's bounds cover, so a root frame keeps its
///        sidebar.
/// @param f The frame: the root frame, or a region over the main pads.
/// @param d The pads to show.
void pads(widgets::grid::GridFrame& f, const PadImage& d);

} // namespace deluge::ui::views::grid
