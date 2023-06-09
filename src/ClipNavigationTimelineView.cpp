/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include <ClipNavigationTimelineView.h>
#include "matrixdriver.h"
#include "song.h"
#include "numericdriver.h"
#include "PadLEDs.h"

int32_t ClipNavigationTimelineView::xScrollBeforeFollowingAutoExtendingLinearRecording; // -1 means none

ClipNavigationTimelineView::ClipNavigationTimelineView() {
}

void ClipNavigationTimelineView::focusRegained() {
	xScrollBeforeFollowingAutoExtendingLinearRecording = -1;
}

int ClipNavigationTimelineView::horizontalEncoderAction(int offset) {

	xScrollBeforeFollowingAutoExtendingLinearRecording = -1;
	return TimelineView::horizontalEncoderAction(offset); // Let parent to scrolling / zooming
}

void ClipNavigationTimelineView::horizontalScrollForLinearRecording(int32_t newXScroll) {
	// Make sure we don't scroll too far right
	if (newXScroll < getMaxLength()) {
		if (!PadLEDs::renderingLock && (!currentUIMode || currentUIMode == UI_MODE_AUDITIONING)
		    && getCurrentUI() == this) {
			initiateXScroll(newXScroll);
		}
		else {
			currentSong->xScroll[NAVIGATION_CLIP] = newXScroll;
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
		}
		if (!numericDriver.popupActive) displayScrollPos();
	}
}
