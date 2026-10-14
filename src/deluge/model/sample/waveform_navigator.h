/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

/// @file
/// @brief Scroll/zoom over a `Sample` that is not tied to a timeline -- the model half of legacy's
///        `WaveformBasicNavigator` (`gui/waveform/waveform_basic_navigator.h:32`).
///
/// **Why this is in `model/` and not `gui/`.** Every statement below is arithmetic over
/// `Sample::lengthInSamples`, `xScroll` and `xZoom`; none of it draws. It moved here so the scenes
/// layer can reach it: `check_layer_rules.py`'s `scenes-includes-outside` row allows `model/` and
/// forbids `gui/`, and `deluge::scenes::marker::MarkerScene` is the second navigator client.
/// `WaveformBasicNavigator` still exists and now derives from this, keeping legacy's one
/// `PadLEDs`-driven responsibility -- the zoom *transition animation* -- on the `gui/` side where
/// it belongs.
///
/// The split is by dependency, not by taste: `WaveformBasicNavigator::zoom`
/// (`waveform_basic_navigator.cpp:53`) is one function whose first two thirds choose a new
/// `xZoom`/`xScroll` and whose last third memcpys `PadLEDs::image` and starts a transition. Only the
/// first two thirds are here.

#include "definitions_cxx.hpp"
#include "model/sample/waveform_render_data.h"

#include <cstdint>

class Sample;
class SampleHolder;

/// @brief One marker's unscrolled position and the column it currently lands on.
///
/// Moved verbatim from `gui/waveform/waveform_renderer.h:31` -- `WaveformNav::zoom` and
/// `WaveformNav::scroll` both take an array of these, so it cannot stay behind in `gui/`.
///
/// `colOnScreen` is a *signed* column that may be far outside `0..kDisplayWidth`; callers use
/// `SampleMarkerEditor::kInvalidColumn` (`-2147483648`) to mean "this marker is not set at all".
struct MarkerColumn {
	int32_t pos; // Unscrolled
	int32_t colOnScreen;
};

namespace deluge::sample {

/// @brief Scroll/zoom state over one `Sample`, plus the cached per-column peaks the scan fills in.
///
/// Legacy's comment, kept because it is still the whole design statement: *this supports basic
/// navigation for samples not tied to a timeline -- i.e. not AudioClips. Navigation is done and
/// thought about at the individual-sample level.*
///
/// @note `renderData` lives here rather than beside the scan because it is *per view*, not per
///       sample: two views of one sample scroll independently and each caches its own columns.
struct WaveformNav {
	Sample* sample{};

	int64_t xZoom{};
	int64_t xScroll{};

	WaveformRenderData renderData;

	/// @brief Adopt @p holder's remembered scroll/zoom, or open zoomed all the way out.
	///
	/// `WaveformBasicNavigator::opened` (`waveform_basic_navigator.cpp:32`). Assumes `sample` is
	/// already set. `renderData.xScroll = -1` is the sentinel that forces the next scan to treat
	/// every column as un-investigated.
	/// @param holder The holder whose `waveformViewScroll`/`waveformViewZoom` to restore, or null.
	void opened(SampleHolder* holder = nullptr);

	/// @brief The zoom at which the whole sample fits across the display.
	/// @return Samples per column, at least 1.
	int32_t max_zoom() const;

	/// @brief Is the view zoomed in at all?
	/// @return True when `xZoom` is not `max_zoom()`.
	bool is_zoomed_in() const;

	/// @brief Zoom in (@p offset >= 0) or out, pinning a marker column if one is on screen.
	///
	/// The arithmetic two thirds of `WaveformBasicNavigator::zoom`
	/// (`waveform_basic_navigator.cpp:53-201`), up to and including
	/// `potentiallyAdjustScrollPosition`. The `PadLEDs` transition that follows it is not here --
	/// see the file docstring.
	///
	/// @param offset                    Signed detents; >= 0 zooms in.
	/// @param shouldAllowExtraScrollRight Passed straight to `adjust_scroll_position`.
	/// @param cols                      `kNumMarkerTypes` marker columns, or null when @p markerType
	///                                  is `MarkerType::NONE`.
	/// @param markerType                Which marker to prefer pinning; `NONE` pins the centre.
	/// @return False when already at the limit, in which case nothing was changed.
	bool zoom(int32_t offset, bool shouldAllowExtraScrollRight = false, const MarkerColumn* cols = nullptr,
	          MarkerType markerType = MarkerType::NONE);

	/// @brief Scroll one column right (@p offset >= 0) or left.
	///
	/// `WaveformBasicNavigator::scroll` (`waveform_basic_navigator.cpp:203`).
	/// @param offset                    Signed detents; only the sign is read.
	/// @param shouldAllowExtraScrollRight Allow scrolling past the end of the waveform.
	/// @param cols                      Marker columns, or null; only `MarkerType::END`'s is read.
	/// @return False when the scroll was refused, in which case nothing was changed.
	bool scroll(int32_t offset, bool shouldAllowExtraScrollRight = false, const MarkerColumn* cols = nullptr);

	/// @brief Clamp `xScroll` back inside the sample.
	///
	/// `WaveformBasicNavigator::potentiallyAdjustScrollPosition` (`waveform_basic_navigator.cpp:242`).
	/// @param shouldAllowExtraScrollRight Skip the right-hand clamp.
	void adjust_scroll_position(bool shouldAllowExtraScrollRight = false);
};

} // namespace deluge::sample
