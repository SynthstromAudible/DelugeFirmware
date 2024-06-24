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

#pragma once

#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "hid/button.h"
#include <cstdint>

class Sample;
class MultisampleRange;
struct MarkerColumn;

// This is just for when we're editing a Sample's loop points etc. It mostly makes use of WaveformBasicNavigator,
// which itself makes use of WaveformRenderer. However, I've implemented this without any class inheritance

class SampleMarkerEditor final : public UI {
public:
	SampleMarkerEditor() = default;

	bool opened() override;
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;
	void selectEncoderAction(int8_t offset) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	void graphicsRoutine() override;
	ActionResult timerCallback() override;
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr,
	                    bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr) override;

	// OLED
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	// 7SEG
	void displayText();

	/// Unlock the loop, allowing the ends to be moved independently
	void loopUnlock();
	/// Lock the loop so the start and end are always the same number of samples apart
	void loopLock();

	MarkerType markerType;

	int8_t blinkPhase;

	int8_t pressX;
	int8_t pressY;

	// ui
	UIType getUIType() override { return UIType::SAMPLE_MARKER_EDITOR; }
	bool exitUI() override;

private:
	static constexpr int32_t kInvalidColumn = -2147483648;
	void writeValue(uint32_t value, MarkerType markerTypeNow = MarkerType::NOT_AVAILABLE);

	int32_t getStartColOnScreen(int32_t unscrolledPos);
	int32_t getEndColOnScreen(int32_t unscrolledPos);
	int32_t getStartPosFromCol(int32_t col);
	int32_t getEndPosFromCol(int32_t col);
	void getColsOnScreen(MarkerColumn* cols);
	void recordScrollAndZoom();
	bool shouldAllowExtraScrollRight();
	/// Render a single column of the sample markers.
	///
	/// @param xDisplay    the column to render in to
	/// @param image       image buffer to render in to
	/// @param cols        Marker column data
	/// @param supressMask Mask of markers to supress rendering for
	void renderColumn(int32_t xDisplay, RGB image[kDisplayHeight][kDisplayWidth + kSideBarWidth],
	                  MarkerColumn cols[kNumMarkerTypes], int32_t supressMask);
	/// Draw a single marker
	void renderMarkerInCol(int32_t xDisplay, RGB thisImage[kDisplayHeight][kDisplayWidth + kSideBarWidth],
	                       MarkerType type, int32_t yStart, int32_t yEnd, bool dimmly);

	/// Swap a marker to its inverse, if the sample is currently reversed.
	[[nodiscard]] MarkerType reverseRemap(MarkerType type) const;
};

extern SampleMarkerEditor sampleMarkerEditor;
