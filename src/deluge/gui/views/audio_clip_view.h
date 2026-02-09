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
	AudioClipView() = default;

	bool opened() override;
	void focusRegained() override;
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	bool setupScroll(uint32_t oldScroll) override;
	void tellMatrixDriverWhichRowsContainSomethingZoomable() override;
	[[nodiscard]] bool supportsTriplets() const override { return false; }
	ClipMinder* toClipMinder() override { return this; }

	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

	void graphicsRoutine() override;
	void playbackEnded() override;

	void clipNeedsReRendering(Clip* clip) override;
	void sampleNeedsReRendering(Sample* sample) override;
	void selectEncoderAction(int8_t offset) override;
	void setClipLengthEqualToSampleLength();
	void adjustLoopLength(int32_t newLength);
	ActionResult horizontalEncoderAction(int32_t offset) override;
	ActionResult editClipLengthWithoutTimestretching(int32_t offset);
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	ActionResult timerCallback() override;
	uint32_t getMaxLength() override;
	uint32_t getMaxZoom() override;

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;

	// ui
	UIType getUIType() override { return UIType::AUDIO_CLIP; }

	// Gate view
	bool gateViewActive{false};

	static constexpr int32_t kGateHeldPadsMax = 8;
	struct GateHeldPad {
		bool active{false};
		int8_t x{-1};
		int8_t y{-1};
		bool wasOpen{false};
		bool encoderUsed{false};
	};
	GateHeldPad gateHeldPads[kGateHeldPadsMax];
	bool gateSpanFillDone{false};

	void adjustGateAttack(int32_t offset);
	void adjustGateRelease(int32_t offset);
	void clearGateHeldPads();
	int32_t findGateHeldPad(int8_t x);
	bool anyGateHeldPadIsOpen();

private:
	uint32_t timeSongButtonPressed;
	void needsRenderingDependingOnSubMode();
	int32_t lastTickSquare;
	bool mustRedrawTickSquares;

	bool endMarkerVisible;   // True if user is currently adjusting the clip's end
	bool startMarkerVisible; // True if user is currently adjusting the clip's start
	bool blinkOn;

	void changeUnderlyingSampleLength(AudioClip& clip, const Sample* sample, int32_t newLength, int32_t oldLength,
	                                  uint64_t oldLengthSamples) const;
	void changeUnderlyingSampleStart(AudioClip& clip, const Sample* sample, int32_t newStartTicks, int32_t oldLength,
	                                 uint64_t oldLengthSamples) const;
};

extern AudioClipView audioClipView;
