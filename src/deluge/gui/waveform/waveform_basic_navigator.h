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

#include "definitions_cxx.hpp"
#include "model/sample/waveform_navigator.h"
#include <cstdint>

class Sample;
struct MarkerColumn;
class SampleHolder;

// This class supports basic navigation for samples not tied to a timeline - i.e. not AudioClips. Navigation is done and
// thought about at the individual-sample level. This is used (but not inherited) by SampleMarkerEditor and
// SampleBrowser
//
// The navigation ARITHMETIC now lives in `deluge::sample::WaveformNav` (model/sample/waveform_navigator.h), which this
// derives from: `sample`, `xScroll`, `xZoom`, `renderData`, `opened()`, `max_zoom()`, `is_zoomed_in()`, `scroll()` and
// `adjust_scroll_position()` are all inherited unchanged. What stays here is the one thing that is not arithmetic --
// the PadLEDs zoom TRANSITION ANIMATION -- which is why `zoom()` is redeclared below and shadows the base's.
class WaveformBasicNavigator : public deluge::sample::WaveformNav {
public:
	WaveformBasicNavigator() = default;

	/// @brief `WaveformNav::zoom` plus legacy's PadLEDs zoom transition.
	///
	/// Shadows (does not override -- the base is not polymorphic) `WaveformNav::zoom`. Every legacy
	/// caller reaches this through the `waveformBasicNavigator` global, whose static type is this
	/// class, so they all get the animation; a `deluge::sample::WaveformNav` of one's own -- which
	/// is what `scenes/marker/` holds -- gets the arithmetic alone.
	/// @param offset                      Signed detents; >= 0 zooms in.
	/// @param shouldAllowExtraScrollRight Allow scrolling past the end of the waveform.
	/// @param cols                        Marker columns, or null.
	/// @param markerType                  Which marker to prefer pinning.
	/// @return False when the zoom was refused; no animation is started in that case.
	bool zoom(int32_t offset, bool shouldAllowExtraScrollRight = false, MarkerColumn* cols = nullptr,
	          MarkerType markerType = MarkerType::NONE);

	/// @brief `WaveformNav::max_zoom()`, under legacy's spelling.
	int32_t getMaxZoom() { return max_zoom(); }
	/// @brief `WaveformNav::is_zoomed_in()`, under legacy's spelling.
	bool isZoomedIn() { return is_zoomed_in(); }
	/// @brief `WaveformNav::adjust_scroll_position()`, under legacy's spelling.
	void potentiallyAdjustScrollPosition(bool shouldAllowExtraScrollRight = false) {
		adjust_scroll_position(shouldAllowExtraScrollRight);
	}
};

extern WaveformBasicNavigator waveformBasicNavigator;
