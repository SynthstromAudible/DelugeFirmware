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

#include "model/sample/waveform_navigator.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multisample_range.h"
#include "util/misc.h"

namespace deluge::sample {

void WaveformNav::opened(SampleHolder* holder) {
	// Only if range is provided, grabs navigation from that if possible

	renderData.xScroll = -1;

	// If want to use saved pos and sample already has peak info stored...
	if (holder && holder->waveformViewZoom) {
		xScroll = holder->waveformViewScroll;
		xZoom = holder->waveformViewZoom;

		adjust_scroll_position();
	}
	else {
		xScroll = 0;
		xZoom = max_zoom();
	}
}

int32_t WaveformNav::max_zoom() const {
	return ((sample->lengthInSamples - 1) >> kDisplayWidthMagnitude) + 1;
}

bool WaveformNav::is_zoomed_in() const {
	return (xZoom != max_zoom());
}

bool WaveformNav::zoom(int32_t offset, bool shouldAllowExtraScrollRight, const MarkerColumn* cols,
                       MarkerType markerType) {
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
		int32_t limit = max_zoom();
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

	adjust_scroll_position(shouldAllowExtraScrollRight);

	return true;
}

bool WaveformNav::scroll(int32_t offset, bool shouldAllowExtraScrollRight, const MarkerColumn* cols) {
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

void WaveformNav::adjust_scroll_position(bool shouldAllowExtraScrollRight) {
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

} // namespace deluge::sample
