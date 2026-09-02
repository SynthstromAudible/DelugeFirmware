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

#include "gui/waveform/waveform_basic_navigator.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/waveform/waveform_renderer.h"
#include "hid/led/pad_leds.h"
#include "model/sample/sample.h"
#include "storage/multi_range/multisample_range.h"
#include "util/misc.h"

#include <string.h>

WaveformBasicNavigator waveformBasicNavigator{};

bool WaveformBasicNavigator::zoom(int32_t offset, bool shouldAllowExtraScrollRight, MarkerColumn* cols,
                                  MarkerType markerType) {
	uint32_t oldScroll = xScroll;
	uint32_t oldZoom = xZoom;

	// The arithmetic (which zoom step, which marker to pin to, where the scroll lands) is
	// `deluge::sample::WaveformNav::zoom`; everything below it is the transition animation.
	if (!WaveformNav::zoom(offset, shouldAllowExtraScrollRight, cols, markerType)) {
		return false;
	}

	const int32_t newXZoom = static_cast<int32_t>(xZoom);

	memcpy(PadLEDs::imageStore[(offset > 0) ? kDisplayHeight : 0], PadLEDs::image,
	       (kDisplayWidth + kSideBarWidth) * kDisplayHeight * sizeof(RGB));

	// Calculate pin squares
	int32_t zoomPinSquareBig = ((int64_t)(int32_t)(oldScroll - xScroll) << 16) / (int32_t)(newXZoom - oldZoom);
	for (int32_t i = 0; i < kDisplayHeight; i++) {
		PadLEDs::zoomPinSquare[i] = zoomPinSquareBig;
		PadLEDs::transitionTakingPlaceOnRow[i] = true;
	}

	int32_t storeOffset = (offset > 0) ? 0 : kDisplayHeight;

	PadLEDs::clearTickSquares(false); // We were mostly fine without this here, but putting it here fixed weird problem
	                                  // where tick squares would
	// appear when zooming into waveform in SampleBrowser

	waveformRenderer.renderFullScreen(sample, xScroll, xZoom, &PadLEDs::imageStore[storeOffset], &renderData);

	PadLEDs::zoomingIn = (offset > 0);
	PadLEDs::zoomMagnitude = PadLEDs::zoomingIn ? offset : -offset;

	currentUIMode |= UI_MODE_HORIZONTAL_ZOOM;
	PadLEDs::recordTransitionBegin(kZoomSpeed);
	PadLEDs::renderZoom();

	return true;
}
