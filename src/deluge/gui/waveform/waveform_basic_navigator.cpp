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

PLACE_SDRAM_BSS WaveformBasicNavigator waveformBasicNavigator{};

WaveformBasicNavigator::WaveformBasicNavigator() {
}

void WaveformBasicNavigator::opened(SampleHolder* holder) {
	// Only if range is provided, grabs navigation from that if possible

	renderData.xScroll = -1;

	// If want to use saved pos and sample already has peak info stored...
	if (holder && holder->waveformViewZoom) {
		xScroll = holder->waveformViewScroll;
		xZoom = holder->waveformViewZoom;

		potentiallyAdjustScrollPosition();
	}
	else {
		xScroll = 0;
		xZoom = getMaxZoom();
	}
}

int32_t WaveformBasicNavigator::getMaxZoom() {
	return ((sample->lengthInSamples - 1) >> kDisplayWidthMagnitude) + 1;
}

bool WaveformBasicNavigator::zoom(int32_t offset, bool shouldAllowExtraScrollRight, MarkerColumn* cols,
                                  MarkerType markerType) {
	uint32_t oldScroll = xScroll;
	uint32_t oldZoom = xZoom;

	int32_t newXZoom;

	// In
	if (offset >= 0) {
		if (xZoom < 2) {
			return false;
		}

		bool isSquareNumber = false;
		uint32_t nextSquareNumber;
		for (int32_t i = 0; i < 31; i++) {
			uint32_t squareNumber = (uint32_t)1 << i;
			if (squareNumber == xZoom) {
				isSquareNumber = true;
				break;
			}
			else if (squareNumber > xZoom) {
				nextSquareNumber = squareNumber;
				break;
			}
		}

		if (!isSquareNumber) {
			if (xZoom >= nextSquareNumber * 0.707) {
				newXZoom = nextSquareNumber >> 1;
			}
			else {
				newXZoom = nextSquareNumber >> 2;
			}
		}

		else {
			newXZoom = xZoom >> 1;
		}
	}

	// Out
	else {
		int32_t limit = getMaxZoom();
		if (xZoom >= limit) {
			return false;
		}
		newXZoom = xZoom << 1;
		if (newXZoom >= limit || (newXZoom * 2) * 0.707 >= limit) {
			newXZoom = limit;
		}
	}

	int32_t pinMarkerCol = -1;
	int32_t pinMarkerPos = xScroll + xZoom * (kDisplayWidth >> 1);
	MarkerType pinnedToMarkerType = MarkerType::NONE;

	if (markerType != MarkerType::NONE) {

		bool foundActiveMarker = false;

		for (int32_t m = 0; m < kNumMarkerTypes; m++) {
			int32_t col = cols[m].colOnScreen;

			if (col >= 0 && col < kDisplayWidth) {

				if (m == util::to_underlying(markerType)) {
bestYet:
					pinMarkerCol = col;
					if (static_cast<MarkerType>(m) >= MarkerType::LOOP_END) {
						pinMarkerCol++; // Pin to right-hand edge of end-marker's col
					}
					pinMarkerPos = cols[m].pos;
					pinnedToMarkerType = static_cast<MarkerType>(m);
				}
				else {
					int32_t pinMarkerDistanceFromCentre = pinMarkerCol - (kDisplayWidth >> 1);
					if (pinMarkerDistanceFromCentre < 0) {
						pinMarkerDistanceFromCentre = -pinMarkerDistanceFromCentre;
					}

					int32_t thisMarkerDistanceFromCentre = col - (kDisplayWidth >> 1);
					if (thisMarkerDistanceFromCentre < 0) {
						thisMarkerDistanceFromCentre = -thisMarkerDistanceFromCentre;
					}

					if (thisMarkerDistanceFromCentre < pinMarkerDistanceFromCentre) {
						goto bestYet;
					}
				}

				if (m == util::to_underlying(markerType)) {
					break;
				}
			}
		}

		// If marker on-screen...
		// if (pinMarkerCol >= 0) {
		// xScroll = pinMarkerPos - newXZoom * pinMarkerCol;
		//}
	}

	if (pinMarkerCol == -1) {
		pinMarkerCol = (kDisplayWidth >> 1);
	}

	xScroll = pinMarkerPos - newXZoom * pinMarkerCol;

	xZoom = newXZoom;

	// Make sure scroll is multiple of zoom
	if (pinnedToMarkerType >= MarkerType::LOOP_END) {
		xScroll = ((xScroll - 1) / xZoom + 1) * xZoom;
	}
	else {
		xScroll = xScroll / xZoom * xZoom;
	}

	potentiallyAdjustScrollPosition(shouldAllowExtraScrollRight);

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

bool WaveformBasicNavigator::scroll(int32_t offset, bool shouldAllowExtraScrollRight, MarkerColumn* cols) {
	// Right
	if (offset >= 0) {

		if (shouldAllowExtraScrollRight) {
			int32_t newScroll = xScroll + xZoom;
			if ((int32_t)(xScroll + xZoom * kDisplayWidth) < xScroll) {
				return false;
			}
			xScroll = newScroll;
		}
		else {
			if (xScroll + xZoom * kDisplayWidth >= sample->lengthInSamples
			    && (!cols || cols[util::to_underlying(MarkerType::END)].colOnScreen < kDisplayWidth)) {
				return false;
			}
			xScroll += xZoom;
		}
	}

	// Left
	else {
		if (xScroll <= 0) {
			return false;
		}
		else if (xScroll < xZoom) {
			xScroll = 0;
		}
		xScroll -= xZoom;
	}

	return true;
}

bool WaveformBasicNavigator::isZoomedIn() {
	return (xZoom != getMaxZoom());
}

void WaveformBasicNavigator::potentiallyAdjustScrollPosition(bool shouldAllowExtraScrollRight) {
	// Make sure not scrolled too far left
	if (xScroll < 0) {
		xScroll = 0;
	}
	else {
		if (!shouldAllowExtraScrollRight) {
			// Make sure not scrolled too far right
			uint32_t lengthInSamples = sample->lengthInSamples;
			int32_t scrollLimit = ((lengthInSamples - 1) / xZoom + 1 - kDisplayWidth) * xZoom;
			if (xScroll > scrollLimit) {
				xScroll = scrollLimit;
			}
		}
	}
}
