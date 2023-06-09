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

#include <WaveformBasicNavigator.h>
#include <WaveformRenderer.h>
#include "definitions.h"
#include "matrixdriver.h"
#include "Sample.h"
#include "MultisampleRange.h"
#include "UI.h"
#include "PadLEDs.h"

WaveformBasicNavigator waveformBasicNavigator;

WaveformBasicNavigator::WaveformBasicNavigator() {
}

void WaveformBasicNavigator::opened(
    SampleHolder* holder) { // Only if range is provided, grabs navigation from that if possible

	renderData.xScroll = -1;

	// If want to use saved pos and sample already has peak info stored...
	if (holder && holder->waveformViewZoom) {
		xScroll = holder->waveformViewScroll;
		xZoom = holder->waveformViewZoom;
	}
	else {
		xScroll = 0;
		xZoom = getMaxZoom();
	}
}

int32_t WaveformBasicNavigator::getMaxZoom() {
	return ((sample->lengthInSamples - 1) >> displayWidthMagnitude) + 1;
}

bool WaveformBasicNavigator::zoom(int offset, bool shouldAllowExtraScrollRight, MarkerColumn* cols, int markerType) {
	uint32_t oldScroll = xScroll;
	uint32_t oldZoom = xZoom;

	int32_t newXZoom;

	// In
	if (offset >= 0) {
		if (xZoom < 2) return false;

		bool isSquareNumber = false;
		uint32_t nextSquareNumber;
		for (int i = 0; i < 31; i++) {
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
			if (xZoom >= nextSquareNumber * 0.707) newXZoom = nextSquareNumber >> 1;
			else newXZoom = nextSquareNumber >> 2;
		}

		else {
			newXZoom = xZoom >> 1;
		}
	}

	// Out
	else {
		int limit = getMaxZoom();
		if (xZoom >= limit) return false;
		newXZoom = xZoom << 1;
		if (newXZoom >= limit || (newXZoom * 2) * 0.707 >= limit) newXZoom = limit;
	}

	int pinMarkerCol = -1;
	int32_t pinMarkerPos = xScroll + xZoom * (displayWidth >> 1);
	int pinnedToMarkerType = MARKER_NONE;

	if (markerType != MARKER_NONE) {

		bool foundActiveMarker = false;

		for (int m = 0; m < NUM_MARKER_TYPES; m++) {
			int col = cols[m].colOnScreen;

			if (col >= 0 && col < displayWidth) {

				if (m == markerType) {
bestYet:
					pinMarkerCol = col;
					if (m >= MARKER_LOOP_END) pinMarkerCol++; // Pin to right-hand edge of end-marker's col
					pinMarkerPos = cols[m].pos;
					pinnedToMarkerType = m;
				}
				else {
					int pinMarkerDistanceFromCentre = pinMarkerCol - (displayWidth >> 1);
					if (pinMarkerDistanceFromCentre < 0) pinMarkerDistanceFromCentre = -pinMarkerDistanceFromCentre;

					int thisMarkerDistanceFromCentre = col - (displayWidth >> 1);
					if (thisMarkerDistanceFromCentre < 0) thisMarkerDistanceFromCentre = -thisMarkerDistanceFromCentre;

					if (thisMarkerDistanceFromCentre < pinMarkerDistanceFromCentre) {
						goto bestYet;
					}
				}

				if (m == markerType) break;
			}
		}

		// If marker on-screen...
		//if (pinMarkerCol >= 0) {
		//xScroll = pinMarkerPos - newXZoom * pinMarkerCol;
		//}
	}

	if (pinMarkerCol == -1) {
		pinMarkerCol = (displayWidth >> 1);
	}

	xScroll = pinMarkerPos - newXZoom * pinMarkerCol;

	xZoom = newXZoom;

	// Make sure scroll is multiple of zoom
	if (pinnedToMarkerType >= MARKER_LOOP_END) xScroll = ((xScroll - 1) / xZoom + 1) * xZoom;
	else xScroll = xScroll / xZoom * xZoom;

	// Make sure not scrolled too far left
	if (xScroll < 0) xScroll = 0;

	else {
		if (!shouldAllowExtraScrollRight) {
			// Make sure not scrolled too far right
			uint32_t lengthInSamples = sample->lengthInSamples;
			int scrollLimit = ((lengthInSamples - 1) / xZoom + 1 - displayWidth) * xZoom;
			if (xScroll > scrollLimit) xScroll = scrollLimit;
		}
	}

	memcpy(PadLEDs::imageStore[(offset > 0) ? displayHeight : 0], PadLEDs::image,
	       (displayWidth + sideBarWidth) * displayHeight * 3);

	// Calculate pin squares
	int32_t zoomPinSquareBig = ((int64_t)(int32_t)(oldScroll - xScroll) << 16) / (int32_t)(newXZoom - oldZoom);
	for (int i = 0; i < displayHeight; i++) {
		PadLEDs::zoomPinSquare[i] = zoomPinSquareBig;
		PadLEDs::transitionTakingPlaceOnRow[i] = true;
	}

	int storeOffset = (offset > 0) ? 0 : displayHeight;

	PadLEDs::clearTickSquares(
	    false); // We were mostly fine without this here, but putting it here fixed weird problem where tick squares would
	            // appear when zooming into waveform in SampleBrowser

	waveformRenderer.renderFullScreen(sample, xScroll, xZoom, &PadLEDs::imageStore[storeOffset], &renderData);

	PadLEDs::zoomingIn = (offset > 0);
	PadLEDs::zoomMagnitude = PadLEDs::zoomingIn ? offset : -offset;

	currentUIMode |= UI_MODE_HORIZONTAL_ZOOM;
	PadLEDs::recordTransitionBegin(zoomSpeed);
	PadLEDs::renderZoom();

	return true;
}

bool WaveformBasicNavigator::scroll(int offset, bool shouldAllowExtraScrollRight, MarkerColumn* cols) {
	// Right
	if (offset >= 0) {

		if (shouldAllowExtraScrollRight) {
			int32_t newScroll = xScroll + xZoom;
			if ((int32_t)(xScroll + xZoom * displayWidth) < xScroll) return false;
			xScroll = newScroll;
		}
		else {
			if (xScroll + xZoom * displayWidth >= sample->lengthInSamples
			    && (!cols || cols[MARKER_END].colOnScreen < displayWidth))
				return false;
			xScroll += xZoom;
		}
	}

	// Left
	else {
		if (xScroll <= 0) return false;
		else if (xScroll < xZoom) xScroll = 0;
		xScroll -= xZoom;
	}

	return true;
}

bool WaveformBasicNavigator::isZoomedIn() {
	return (xZoom != getMaxZoom());
}
