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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/root_ui.h"
#include "hid/button.h"

class InstrumentClip;
class NoteRow;

class TimelineView : public RootUI {
public:
	TimelineView() = default;

	void scrollFinished();

	TimelineView* toTimelineView() final { return this; }

	virtual uint32_t getMaxZoom() = 0;
	virtual bool calculateZoomPinSquares(uint32_t oldScroll, uint32_t newScroll, uint32_t newZoom,
	                                     uint32_t oldZoom); // Returns false if no animation needed
	virtual uint32_t getMaxLength() = 0;
	virtual bool setupScroll(uint32_t oldScroll); // Returns false if no animation needed
	[[nodiscard]] virtual int32_t getNavSysId() const { return NAVIGATION_CLIP; }

	virtual void tellMatrixDriverWhichRowsContainSomethingZoomable() {
	} // SessionView doesn't have this because it does this a different way. Sorry, confusing I know

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	void displayZoomLevel(bool justPopup = false);
	ActionResult horizontalEncoderAction(int32_t offset) override;
	void displayScrollPos();
	void displayNumberOfBarsAndBeats(uint32_t number, uint32_t quantization, bool countFromOne,
	                                 char const* tooLongText);
	void initiateXScroll(uint32_t newXScroll, int32_t numSquaresToScroll = kDisplayWidth);
	bool zoomToMax(bool inOnly = false);
	void initiateXZoom(int32_t zoomMagnitude, int32_t newScroll, uint32_t oldZoom);
	void midiLearnFlash();

	bool scrollRightToEndOfLengthIfNecessary(int32_t maxLength);

	bool scrollLeftIfTooFarRight(int32_t maxLength);

	void tripletsButtonPressed();
	void setTripletsLEDState();

	[[nodiscard]] int32_t getPosFromSquare(int32_t square, int32_t localScroll = -1) const;
	[[nodiscard]] int32_t getPosFromSquare(int32_t square, int32_t xScroll, uint32_t xZoom) const;
	int32_t getSquareFromPos(int32_t pos, bool* rightOnSquare = NULL, int32_t localScroll = -1);
	int32_t getSquareFromPos(int32_t pos, bool* rightOnSquare, int32_t xScroll, uint32_t xZoom);
	int32_t getSquareEndFromPos(int32_t pos, int32_t localScroll = -1);
	bool isSquareDefined(int32_t square, int32_t xScroll = -1);
	bool isSquareDefined(int32_t square, int32_t xScroll, uint32_t xZoom);

	[[nodiscard]] bool inTripletsView() const;

private:
	/// Used when scrolling horizontally to briefly catch on clip's max zoom
	uint32_t delayHorizontalZoomUntil = 0;
	/// Horizontal scroll is only delayed in the direction that clip's max zoom was crossed in.
	/// This is the direction (-1 or +1).
	int8_t delayHorizontalZoomMagnitude;
};
