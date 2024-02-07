/*
 * Copyright © 2014-2024 Synthstrom Audible Limited
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
#include "gui/views/clip_view.h"
#include "hid/button.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/automation/copied_param_automation.h"

class Action;
class CopiedNoteRow;
class Drum;
class Editor;
class AudioClip;
class Instrument;
class InstrumentClip;
class MidiInstrument;
class ModControllable;
class ModelStackWithAutoParam;
class ModelStackWithNoteRow;
class ModelStackWithThreeMainThings;
class ModelStackWithTimelineCounter;
class Note;
class NoteRow;
class ParamCollection;
class ParamManagerForTimeline;
class ParamNode;
class Sound;
class SoundDrum;

class AutomationView final : public ClipView, public InstrumentClipMinder {
public:
	AutomationView();
	bool opened();
	void openedInBackground();
	void focusRegained();

	// called by ui_timer_manager - might need to revise this routine for automation clip view since it references notes
	void graphicsRoutine();

	// ui
	UIType getUIType() { return UIType::AUTOMATION_VIEW; }
	AutomationSubType getAutomationSubType();

	// rendering
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void renderDisplay(int32_t knobPosLeft = kNoSelection, int32_t knobPosRight = kNoSelection,
	                   bool modEncoderAction = false);
	void displayAutomation(bool padSelected = false, bool updateDisplay = true);

	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) { InstrumentClipMinder::renderOLED(image); }

	// button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);

	// pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);

	// horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset);
	uint32_t getMaxLength();
	uint32_t getMaxZoom();
	int32_t getNavSysId();
	int32_t navSysId;

	// vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	ActionResult scrollVertical(int32_t scrollAmount);

	// mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	bool modEncoderActionForSelectedPad(ModelStackWithAutoParam* modelStackWithParam, int32_t whichModEncoder,
	                                    int32_t offset, int32_t effectiveLength);
	void modEncoderActionForUnselectedPad(ModelStackWithAutoParam* modelStackWithParam, int32_t whichModEncoder,
	                                      int32_t offset, int32_t effectiveLength);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	CopiedParamAutomation copiedParamAutomation;

	// tempo encoder action
	void tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed);

	// Select encoder action
	void selectEncoderAction(int8_t offset);

	// called by melodic_instrument.cpp or kit.cpp
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow);

	// called by playback_handler.cpp
	void notifyPlaybackBegun();

	// not sure how this is used
	ClipMinder* toClipMinder() { return this; }

	bool isOnAutomationOverview();

	bool interpolation;
	bool interpolationBefore;
	bool interpolationAfter;

	// public to midi follow can access it
	ModelStackWithAutoParam*
	getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                              int32_t paramID = deluge::modulation::params::kNoParamID,
	                              deluge::modulation::params::Kind paramKind = deluge::modulation::params::Kind::NONE);

	// public so instrument clip view can access it
	void initParameterSelection();
	bool onArrangerView;

private:
	// button action functions
	bool handleScaleButtonAction(InstrumentClip* instrumentClip, OutputType outputType, bool on);
	void handleSessionButtonAction(Clip* clip, bool on);
	void handleKeyboardButtonAction(bool on);
	void handleClipButtonAction(bool on, bool isAudioClip);
	void handleCrossScreenButtonAction(bool on);
	void handleKitButtonAction(OutputType outputType, bool on);
	void handleSynthButtonAction(OutputType outputType, bool on);
	void handleMidiButtonAction(OutputType outputType, bool on);
	void handleCVButtonAction(OutputType outputType, bool on);
	bool handleHorizontalEncoderButtonAction(bool on, bool isAudioClip);
	bool handleBackAndHorizontalEncoderButtonComboAction(Clip* clip, bool on);
	void handleVerticalEncoderButtonAction(bool on);
	void handleSelectEncoderButtonAction(bool on);

	// audition pad action
	ActionResult handleAuditionPadAction(InstrumentClip* instrumentClip, Output* output, OutputType outputType,
	                                     int32_t y, int32_t velocity);
	void auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown);

	// mute pad action
	ActionResult handleMutePadAction(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                 InstrumentClip* instrumentClip, Output* output, OutputType outputType, int32_t y,
	                                 int32_t velocity);

	// edit pad action
	ActionResult handleEditPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, OutputType outputType,
	                                 int32_t effectiveLength, int32_t x, int32_t y, int32_t velocity);
	void editPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, bool state, uint8_t yDisplay,
	                   uint8_t xDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);

	// Automation View Render Functions
	void performActualRender(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                         int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
	                         bool drawUndefinedArea = true);
	void renderAutomationOverview(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                              ModelStackWithThreeMainThings* modelStackWithThreeMainThings, Clip* clip,
	                              OutputType outputType, RGB* image, uint8_t occupancyMask[], int32_t yDisplay);
	void renderAutomationEditor(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, RGB* image,
	                            uint8_t occupancyMask[], int32_t renderWidth, int32_t xScroll, uint32_t xZoom,
	                            int32_t effectiveLength, int32_t yDisplay, bool drawUndefinedArea);
	void renderRow(ModelStackWithAutoParam* modelStackWithParam, RGB* image, uint8_t occupancyMask[],
	               int32_t lengthToDisplay, int32_t yDisplay, bool isAutomated, int32_t xScroll, int32_t xZoom);
	void renderUndefinedArea(int32_t localScroll, uint32_t, int32_t lengthToDisplay, RGB* image, uint8_t[],
	                         int32_t imageWidth, TimelineView* editorScreen, bool tripletsOnHere);
	void renderLove(RGB* image, uint8_t occupancyMask[], int32_t yDisplay = 0);
	void renderDisplayOLED(Clip* clip, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       int32_t knobPosRight = kNoSelection);
	void renderDisplay7SEG(Clip* clip, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       bool modEncoderAction = false);

	// Enter/Exit Scale Mode
	void enterScaleMode(uint8_t yDisplay = 255);
	void exitScaleMode();

	// Horizontal Encoder Action
	void shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
	                                 int32_t effectiveLength);

	// Mod Encoder Action
	void copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xScroll, int32_t xZoom);
	void pasteAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t effectiveLength,
	                     int32_t xScroll, int32_t xZoom);

	// Select Encoder Action
	void selectGlobalParam(int32_t offset, Clip* clip);
	void selectNonGlobalParam(int32_t offset, Clip* clip);
	void selectMIDICC(int32_t offset, Clip* clip);
	int32_t getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
	                                          int32_t numParams);
	void getLastSelectedParamShortcut(Clip* clip, OutputType outputType);

	// Automation Lanes Functions
	void initPadSelection();
	void initInterpolation();
	int32_t getEffectiveLength(ModelStackWithTimelineCounter* modelStack);
	uint32_t getSquareWidth(int32_t square, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	uint32_t getMiddlePosFromSquare(int32_t xDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	int32_t getAverageSquareKnobPosition(ModelStackWithAutoParam* modelStack, int32_t xDisplay, int32_t effectiveLength,
	                                     int32_t xScroll, int32_t xZoom);

	void getParameterName(Clip* clip, OutputType outputType, char* parameterName);
	int32_t getParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t pos);

	bool getNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos, bool reversed);
	void setParameterAutomationValue(ModelStackWithAutoParam* modelStack, int32_t knobPos, int32_t squareStart,
	                                 int32_t xDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                 bool modEncoderAction = false);
	void setKnobIndicatorLevels(int32_t knobPos);
	void updateModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart, bool updateDisplay = true,
	                       bool updateIndicatorLevels = true);

	bool recordSinglePadPress(int32_t xDisplay, int32_t yDisplay);
	void handleSinglePadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xDisplay,
	                          int32_t yDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                          bool shortcutPress = false);
	bool handleParameterSelection(Clip* clip, OutputType outputType, int32_t xDisplay, int32_t yDisplay);
	void handleParameterAutomationChange(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                                     OutputType outputType, int32_t xDisplay, int32_t yDisplay,
	                                     int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	int32_t calculateKnobPosForSinglePadPress(OutputType outputType, int32_t yDisplay);

	void handleMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t firstPadX,
	                         int32_t firstPadY, int32_t secondPadX, int32_t secondPadY, int32_t effectiveLength,
	                         int32_t xScroll, int32_t xZoom, bool modEncoderAction = false);
	void renderDisplayForMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                                   int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                   int32_t xDisplay = kNoSelection, bool modEncoderAction = false);

	int32_t calculateKnobPosForModEncoderTurn(int32_t knobPos, int32_t offset);
	void displayCVErrorMessage();
	void resetShortcutBlinking();

	bool encoderAction;
	bool shortcutBlinking;

	bool padSelectionOn;
	bool multiPadPressSelected;
	bool multiPadPressActive;
	bool middlePadPressSelected;
	int32_t leftPadSelectedX;
	int32_t leftPadSelectedY;
	int32_t rightPadSelectedX;
	int32_t rightPadSelectedY;
	int32_t lastPadSelectedKnobPos;

	bool playbackStopped;

	// grid sized array to assign midi cc values to each pad on the grid
	void initMIDICCShortcutsForAutomation();
	uint32_t midiCCShortcutsForAutomation[kDisplayWidth][kDisplayHeight];
	bool midiCCShortcutsLoaded;
};

extern AutomationView automationView;
