/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#ifndef EDITORSCREEN_H
#define EDITORSCREEN_H

#include <RootUI.h>
#include "definitions.h"

class InstrumentClip;
class NoteRow;

class TimelineView : public RootUI {
public:
	TimelineView() {}

	void scrollFinished();

	virtual unsigned int getMaxZoom() = 0;
	virtual bool calculateZoomPinSquares(uint32_t oldScroll, uint32_t newScroll, uint32_t newZoom,
	                                     uint32_t oldZoom); // Returns false if no animation needed
	virtual uint32_t getMaxLength() = 0;
	virtual bool setupScroll(uint32_t oldScroll); // Returns false if no animation needed
	virtual int getNavSysId() { return NAVIGATION_CLIP; }

	virtual void tellMatrixDriverWhichRowsContainSomethingZoomable() {
	} // SessionView doesn't have this because it does this a different way. Sorry, confusing I know
	bool isTimelineView() { return true; }

	int buttonAction(int x, int y, bool on, bool inCardRoutine);
	void displayZoomLevel(bool justPopup = false);
	int horizontalEncoderAction(int offset);
	void displayScrollPos();
	void displayNumberOfBarsAndBeats(uint32_t number, uint32_t quantization, bool countFromOne,
	                                 char const* tooLongText);
	void initiateXScroll(uint32_t newXScroll, int numSquaresToScroll = displayWidth);
	bool zoomToMax(bool inOnly = false);
	void initiateXZoom(int zoomMagnitude, int32_t newScroll, uint32_t oldZoom);
	void midiLearnFlash();

	bool scrollRightToEndOfLengthIfNecessary(int32_t maxLength);

	bool scrollLeftIfTooFarRight(int32_t maxLength);

	void tripletsButtonPressed();

	int32_t getPosFromSquare(int32_t square, int32_t localScroll = -1);
	int32_t getPosFromSquare(int32_t square, int32_t xScroll, uint32_t xZoom);
	int32_t getSquareFromPos(int32_t pos, bool* rightOnSquare = NULL, int32_t localScroll = -1);
	int32_t getSquareFromPos(int32_t pos, bool* rightOnSquare, int32_t xScroll, uint32_t xZoom);
	int32_t getSquareEndFromPos(int32_t pos, int32_t localScroll = -1);
	bool isSquareDefined(int square, int32_t xScroll = -1);
	bool isSquareDefined(int square, int32_t xScroll, uint32_t xZoom);

	bool inTripletsView();
};

#endif // EDITORSCREEN_H
