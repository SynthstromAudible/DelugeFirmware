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

#include "gui/views/clip_view.h"
#include "hid/button.h"
#include "model/clip/clip_minder.h"
#include <cstdint>

class AudioClip;

class AudioClipView final : public ClipView, public ClipMinder {
public:
	AudioClipView();

	bool opened();
	void focusRegained();
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	bool setupScroll(uint32_t oldScroll);
	void tellMatrixDriverWhichRowsContainSomethingZoomable();
	bool supportsTriplets() { return false; }
	ClipMinder* toClipMinder() { return this; }

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);

	void graphicsRoutine();
	void playbackEnded();

	void clipNeedsReRendering(Clip* clip);
	void sampleNeedsReRendering(Sample* sample);
	void selectEncoderAction(int8_t offset);
	void adjustLoopLength(int32_t newLength);
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	ActionResult timerCallback();
	uint32_t getMaxLength();
	uint32_t getMaxZoom();

	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);

	// ui
	UIType getUIType() { return UIType::AUDIO_CLIP_VIEW; }

private:
	void needsRenderingDependingOnSubMode();
	int32_t lastTickSquare;
	bool mustRedrawTickSquares;
	bool endMarkerVisible;
	bool blinkOn;
	void changeUnderlyingSampleLength(AudioClip* clip, const Sample* sample, int32_t newLength) const;
};

extern AudioClipView audioClipView;
