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
#include "gui/views/clip_view.h"
#include "hid/button.h"
#include "model/clip/instrument_clip_minder.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/params/param_node.h"

class Action;
class CopiedNoteRow;
class Drum;
class Editor;
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

class AutomationInstrumentClipView final : public ClipView, public InstrumentClipMinder, public ModControllableAudio {
public:
	AutomationInstrumentClipView();
	bool opened();
	void openedInBackground();
	void focusRegained();

	//called by ui_timer_manager - might need to revise this routine for automation clip view since it references notes
	void graphicsRoutine();

	//rendering
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void renderDisplay(int32_t knobPosLeft = kNoSelection, int32_t knobPosRight = kNoSelection);
	void displayAutomation(bool padSelected = false);

	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) { InstrumentClipMinder::renderOLED(image); }

	//button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);

	//pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity);

	//edit pad action
	void editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, uint32_t xZoom);

	//audition pad action
	void auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown);

	//horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset);

	//vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	ActionResult scrollVertical(int32_t scrollAmount, bool inCardRoutine, bool shiftingNoteRow = false);

	//mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	CopiedParamAutomation copiedParamAutomation;

	//tempo encoder action
	void tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed);

	//Select encoder action
	void selectEncoderAction(int8_t offset);

	//called by melodic_instrument.cpp or kit.cpp
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow);

	//called by playback_handler.cpp
	void notifyPlaybackBegun();

	//not sure how this is used
	ClipMinder* toClipMinder() { return this; }

	bool isOnAutomationOverview();

	bool interpolation;
	bool interpolationBefore;
	bool interpolationAfter;

private:
	//Automation View Render Functions
	void performActualRender(uint32_t whichRows, uint8_t* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                         int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
	                         bool drawUndefinedArea = true);
	void renderAutomationOverview(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip,
	                              Instrument* instrument, uint8_t* image, uint8_t occupancyMask[],
	                              int32_t yDisplay = 0);
	void renderAutomationEditor(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip, Instrument* instrument,
	                            uint8_t* image, uint8_t occupancyMask[], int32_t renderWidth, int32_t xScroll,
	                            uint32_t xZoom, int32_t yDisplay = 0, bool drawUndefinedArea = true);
	void renderRow(ModelStackWithAutoParam* modelStack, uint8_t* image, uint8_t occupancyMask[], bool, uint32_t,
	               bool allowNoteTails, int32_t xScroll, uint32_t xZoom, int32_t xStart = 0,
	               int32_t xEnd = kDisplayWidth, bool drawRepeats = false, int32_t yDisplay = 0,
	               bool isAutomated = false);
	void renderLove(uint8_t* image, uint8_t occupancyMask[], int32_t yDisplay = 0);

	//Enter/Exit Scale Mode
	void enterScaleMode(uint8_t yDisplay = 255);
	void exitScaleMode();

	//Horizontal Encoder Action
	void shiftAutomationHorizontally(int32_t offset);

	//Mod Encoder Action
	void copyAutomation();
	void pasteAutomation();

	//Automation Lanes Functions
	void initParameterSelection();
	void initPadSelection();
	void initInterpolation();
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip,
	                                                int32_t paramID = 0xFFFFFFFF,
	                                                Param::Kind paramKind = Param::Kind::NONE);

	void getParameterName(char* parameterName);
	int32_t getParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t pos);

	bool getNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos, bool reversed);
	void setParameterAutomationValue(ModelStackWithAutoParam* modelStack, int32_t knobPos, int32_t squareStart,
	                                 int32_t xDisplay, int32_t effectiveLength);
	void setKnobIndicatorLevels(int32_t knobPos);

	bool recordSinglePadPress(int32_t xDisplay, int32_t yDisplay);
	void handleSinglePadPress(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip, int32_t xDisplay,
	                          int32_t yDisplay, bool shortcutPress = false);
	int32_t calculateKnobPosForSinglePadPress(int32_t yDisplay);

	void handleMultiPadPress(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip, int32_t firstPadX,
	                         int32_t firstPadY, int32_t secondPadX, int32_t secondPadY, bool modEncoderAction = false);

	int32_t calculateKnobPosForModEncoderTurn(int32_t knobPos, int32_t offset);
	void displayCVErrorMessage();
	void resetShortcutBlinking();

	bool encoderAction;
	bool shortcutBlinking;

	bool padSelectionOn;
	bool multiPadPressSelected;
	bool multiPadPressActive;
	int32_t leftPadSelectedX;
	int32_t leftPadSelectedY;
	int32_t rightPadSelectedX;
	int32_t rightPadSelectedY;
	int32_t lastPadSelectedKnobPos;
};

extern AutomationInstrumentClipView automationInstrumentClipView;
