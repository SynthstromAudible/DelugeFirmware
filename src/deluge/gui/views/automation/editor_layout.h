/*
 * Copyright (c) 2023 Sean Ditny
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

#include "gui/views/automation_view.h"

// namespace deluge::gui::views::automation {

class AutomationEditorLayout {
public:
	AutomationEditorLayout() = default;
	virtual ~AutomationEditorLayout() {}

	// get automationView class info
protected:
	// get AutomationView UI
	inline RootUI* getAutomationView() { return &automationView; }
	inline AutomationParamType& getAutomationParamType() { return automationView.automationParamType; }
	inline bool& getOnArrangerView() { return automationView.onArrangerView; }
	inline int32_t& getNavSysId() { return automationView.navSysId; }

	// display / LED indicators / pad rendering
	inline void renderDisplay(int32_t knobPosLeft = kNoSelection, int32_t knobPosRight = kNoSelection,
	                          bool modEncoderAction = false) {
		automationView.renderDisplay(knobPosLeft, knobPosRight, modEncoderAction);
	}
	inline void displayAutomation(bool padSelected = false, bool updateDisplay = true) {
		automationView.displayAutomation(padSelected, updateDisplay);
	}
	inline void renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay,
	                                RGB image[][kDisplayWidth + kSideBarWidth],
	                                uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t imageWidth,
	                                TimelineView* timelineView, bool tripletsOnHere, int32_t xDisplay) {
		automationView.renderUndefinedArea(xScroll, xZoom, lengthToDisplay, image, occupancyMask, imageWidth,
		                                   timelineView, tripletsOnHere, xDisplay);
	}

	// interpolation
	inline void initInterpolation() { automationView.initInterpolation(); }
	inline void resetInterpolationShortcutBlinking() { automationView.resetInterpolationShortcutBlinking(); }
	inline void blinkInterpolationShortcut() { automationView.blinkInterpolationShortcut(); }
	inline bool& getInterpolation() { return automationView.interpolation; }
	inline bool& getInterpolationBefore() { return automationView.interpolationBefore; }
	inline bool& getInterpolationAfter() { return automationView.interpolationAfter; }

	// pad selection mode
	inline bool& getPadSelectionOn() { return automationView.padSelectionOn; }
	inline void initPadSelection() { automationView.initPadSelection(); }
	inline void blinkPadSelectionShortcut() { automationView.blinkPadSelectionShortcut(); }

	// pad press
	inline int32_t getPosFromSquare(int32_t square, int32_t localScroll = -1) const {
		return automationView.getPosFromSquare(square, localScroll);
	}
	inline int32_t getPosFromSquare(int32_t square, int32_t xScroll, uint32_t xZoom) const {
		return automationView.getPosFromSquare(square, xScroll, xZoom);
	}
	inline bool& getMultiPadPressActive() { return automationView.multiPadPressActive; }
	inline bool& getMultiPadPressSelected() { return automationView.multiPadPressSelected; }
	inline bool& getMiddlePadPressSelected() { return automationView.middlePadPressSelected; }
	inline int32_t& getLeftPadSelectedX() { return automationView.leftPadSelectedX; }
	inline int32_t& getLeftPadSelectedY() { return automationView.leftPadSelectedY; }
	inline int32_t& getRightPadSelectedX() { return automationView.rightPadSelectedX; }
	inline int32_t& getRightPadSelectedY() { return automationView.rightPadSelectedY; }
	inline int32_t& getLastPadSelectedKnobPos() { return automationView.lastPadSelectedKnobPos; }

	// mod encoder
	inline CopiedParamAutomation* getCopiedParamAutomation() { return &automationView.copiedParamAutomation; }

	// model stack
	inline ModelStackWithAutoParam*
	getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                              int32_t paramID = deluge::modulation::params::kNoParamID,
	                              deluge::modulation::params::Kind paramKind = deluge::modulation::params::Kind::NONE) {
		return automationView.getModelStackWithParamForClip(modelStack, clip, paramID, paramKind);
	}
};

// }; // namespace deluge::gui::views::automation
