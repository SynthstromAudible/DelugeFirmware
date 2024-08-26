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

#include "definitions_cxx.hpp"
#include "gui/views/clip_navigation_timeline_view.h"
#include "hid/button.h"
#include "modulation/params/param.h"
#include "storage/storage_manager.h"

class Editor;
class InstrumentClip;
class Clip;
class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithAutoParam;

struct PadPress {
	bool isActive;
	int32_t xDisplay;
	int32_t yDisplay;
	deluge::modulation::params::Kind paramKind;
	int32_t paramID;
};

struct FXColumnPress {
	int32_t previousKnobPosition;
	int32_t currentKnobPosition;
	int32_t yDisplay;
	uint32_t timeLastPadPress;
	bool padPressHeld;
};

struct ParamsForPerformance {
	deluge::modulation::params::Kind paramKind = deluge::modulation::params::Kind::NONE;
	deluge::modulation::params::ParamType paramID;
	int32_t xDisplay = kNoSelection;
	int32_t yDisplay = kNoSelection;
	RGB rowColour = deluge::gui::colours::black;
	RGB rowTailColour = deluge::gui::colours::black;
};

class PerformanceView final : public ClipNavigationTimelineView {
public:
	PerformanceView();
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows);
	bool opened() override;
	void focusRegained() override;

	void graphicsRoutine() override;
	ActionResult timerCallback() override;

	// ui
	UIType getUIType() override { return UIType::PERFORMANCE; }
	UIType getUIContextType() override;
	UIModControllableContext getUIModControllableContext() override { return UIModControllableContext::SONG; }
	[[nodiscard]] int32_t getNavSysId() const override;

	// rendering
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void renderViewDisplay();
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	// 7SEG only
	void redrawNumericDisplay();
	void setLedStates();

	// button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

	// pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

	// horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset) override;

	// vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;

	// mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on) override;
	void modButtonAction(uint8_t whichButton, bool on) override;

	// select encoder action
	void selectEncoderAction(int8_t offset) override;

	// not sure why we need these...
	uint32_t getMaxZoom() override;
	uint32_t getMaxLength() override;

	// public so soundEditor can access it
	void savePerformanceViewLayout();
	void loadPerformanceViewLayout();
	void updateLayoutChangeStatus();
	void resetPerformanceView(ModelStackWithThreeMainThings* modelStack);
	bool defaultEditingMode;
	bool editingParam; // if you're not editing a param, you're editing a value
	bool justExitedSoundEditor;

	// public so Action Logger can access it
	FXColumnPress fxPress[kDisplayWidth];

	// public so view.modEncoderAction and midi follow can access it
	PadPress lastPadPress;
	void renderFXDisplay(deluge::modulation::params::Kind paramKind, int32_t paramID, int32_t knobPos = kNoSelection);
	bool onFXDisplay;

	uint32_t timeKeyboardShortcutPress;

	// public so mod controllable audio and midi follow can access it
	bool possiblyRefreshPerformanceViewDisplay(deluge::modulation::params::Kind kind, int32_t id, int32_t newKnobPos);

private:
	// initialize
	void initPadPress(PadPress& padPress);
	void initFXPress(FXColumnPress& columnPress);
	void initLayout(ParamsForPerformance& layout);
	void initDefaultFXValues(int32_t xDisplay);

	// rendering
	void renderRow(RGB* image, int32_t yDisplay);
	bool isParamAssignedToFXColumn(deluge::modulation::params::Kind paramKind, int32_t paramID);
	void setCentralLEDStates();

	// pad action
	void normalPadAction(ModelStackWithThreeMainThings* modelStack, int32_t xDisplay, int32_t yDisplay, int32_t on);
	void paramEditorPadAction(ModelStackWithThreeMainThings* modelStack, int32_t xDisplay, int32_t yDisplay,
	                          int32_t on);
	bool isPadShortcut(int32_t xDisplay, int32_t yDisplay);
	bool setParameterValue(ModelStackWithThreeMainThings* modelStack, deluge::modulation::params::Kind paramKind,
	                       int32_t paramID, int32_t xDisplay, int32_t knobPos, bool renderDisplay = true);
	void getParameterValue(ModelStackWithThreeMainThings* modelStack, deluge::modulation::params::Kind paramKind,
	                       int32_t paramID, int32_t xDisplay, bool renderDisplay = true);
	void padPressAction(ModelStackWithThreeMainThings* modelStack, deluge::modulation::params::Kind paramKind,
	                    int32_t paramID, int32_t xDisplay, int32_t yDisplay, bool renderDisplay = true);
	void padReleaseAction(ModelStackWithThreeMainThings* modelStack, deluge::modulation::params::Kind paramKind,
	                      int32_t paramID, int32_t xDisplay, bool renderDisplay = true);
	void resetFXColumn(ModelStackWithThreeMainThings* modelStack, int32_t xDisplay);
	void releaseViewOnExit(ModelStackWithThreeMainThings* modelStack);
	void resetPadPressInfo();
	void releaseStutter(ModelStackWithThreeMainThings* modelStack);

	/// write/load default values
	void writeDefaultsToFile();
	void writeDefaultFXValuesToFile(Serializer& writer);
	void writeDefaultFXParamToFile(Serializer& writer, int32_t xDisplay);
	void writeDefaultFXRowValuesToFile(Serializer& writer, int32_t xDisplay);
	void writeDefaultFXHoldStatusToFile(Serializer& writer, int32_t xDisplay);
	void loadDefaultLayout();
	void readDefaultsFromBackedUpFile();
	void readDefaultsFromFile();
	void readDefaultFXValuesFromFile();
	void readDefaultFXParamAndRowValuesFromFile(int32_t xDisplay);
	void readDefaultFXParamFromFile(int32_t xDisplay);
	void readDefaultFXRowNumberValuesFromFile(int32_t xDisplay);
	void readDefaultFXHoldStatusFromFile(int32_t xDisplay);
	void initializeHeldFX(int32_t xDisplay);
	bool successfullyReadDefaultsFromFile;
	bool anyChangesToSave;

	/// backup loaded layout (what's currently in XML file)
	/// backup the last loaded/last saved changes, so you can compare and let user know if any changes
	/// need to be saved
	FXColumnPress backupXMLDefaultFXPress[kDisplayWidth];
	ParamsForPerformance backupXMLDefaultLayoutForPerformance[kDisplayWidth];
	int32_t backupXMLDefaultFXValues[kDisplayWidth][kDisplayHeight];

	int32_t getKnobPosForSinglePadPress(int32_t xDisplay, int32_t yDisplay);
	int32_t calculateKnobPosForSelectEncoderTurn(int32_t knobPos, int32_t offset);

	PadPress firstPadPress;
	ParamsForPerformance layoutForPerformance[kDisplayWidth];
	int32_t defaultFXValues[kDisplayWidth][kDisplayHeight];
	int32_t layoutBank;    // A or B (assign a layout to the bank for cross fader action)
	int32_t layoutVariant; // 1, 2, 3, 4, 5 (1 = Load, 2 = Synth, 3 = Kit, 4 = Midi, 5 = CV)

	// backup current layout
	void backupPerformanceLayout();
	bool performanceLayoutBackedUp;
	bool shouldRestorePreviousHoldPress(int32_t xDisplay);
	void restorePreviousHoldPress(int32_t xDisplay);
	void logPerformanceViewPress(int32_t xDisplay, bool closeAction = true);
	bool anyChangesToLog();
	FXColumnPress backupFXPress[kDisplayWidth];

	// Members regarding rendering different layouts
private:
	bool sessionButtonActive = false;
	bool sessionButtonUsed = false;
};

extern PerformanceView performanceView;
