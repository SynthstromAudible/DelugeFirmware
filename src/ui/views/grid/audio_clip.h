#pragma once
#include "ui/widgets/grid/frame.h"

#include <cstdint>

/// @file
/// @brief An audio clip's start and end markers on the main pads, and the grey past its end.

namespace deluge::ui::views::grid {

/// @brief Where the clip's two markers fall on the main pads, and how they show this pass.
struct ClipMarkers {
	int32_t start_square; ///< The clip's first square, in main-pad columns from the left edge; off-screen allowed.
	int32_t end_square;   ///< The clip's last square, likewise.
	bool start_visible;   ///< The start marker is lit at all (it is the one being edited, or always shown).
	bool end_visible;     ///< Likewise the end marker.
	bool blink_on;        ///< The markers' blink is in its lit half: full colour; dim otherwise.
};

/// @brief `AudioClipView::renderMainPads`' "grey regions + flashing columns" loop over the painted waveform: the end
///        marker red (dull while the blink is off) and grey past it; the start marker green (dimmed while off), or the
///        whole width grey once the start has scrolled off the right.
/// @param main A region over the main pads, painted with the waveform already.
/// @param d    Where the markers are.
void clip_markers(widgets::grid::GridFrame& main, const ClipMarkers& d);

} // namespace deluge::ui::views::grid
