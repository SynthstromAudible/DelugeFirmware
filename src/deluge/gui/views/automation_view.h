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
#include "model/note/note_row.h"
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
class PatchCableSet;
class Sound;
class SoundDrum;

class AutomationView final : public ClipView, public InstrumentClipMinder {
public:
	AutomationView();
	bool opened() override;
	void initializeView();
	void openedInBackground();
	void focusRegained() override;

	// called by ui_timer_manager - might need to revise this routine for automation clip view since it references notes
	void graphicsRoutine() override;

	// ui
	UIType getUIType() override { return UIType::AUTOMATION; }
	AutomationSubType getAutomationSubType();
	const char* getName() override { return "automation_view"; }

	// rendering
	bool possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind, int32_t paramID);
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void renderDisplay(int32_t knobPosLeft = kNoSelection, int32_t knobPosRight = kNoSelection,
	                   bool modEncoderAction = false);
	void displayAutomation(bool padSelected = false, bool updateDisplay = true);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override {
		InstrumentClipMinder::renderOLED(canvas);
	}

	// button action
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;

	// pad action
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

	// horizontal encoder action
	ActionResult horizontalEncoderAction(int32_t offset) override;
	uint32_t getMaxLength() override;
	uint32_t getMaxZoom() override;
	[[nodiscard]] int32_t getNavSysId() const override;
	int32_t navSysId;

	// vertical encoder action
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine);
	ActionResult scrollVertical(int32_t scrollAmount);
	void potentiallyVerticalScrollToSelectedDrum(InstrumentClip* clip, Output* output);

	// mod encoder action
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on) override;
	CopiedParamAutomation copiedParamAutomation;

	// Select encoder action
	void selectEncoderAction(int8_t offset) override;
	void getLastSelectedParamShortcut(Clip* clip);      // public so menu can access it
	void getLastSelectedParamArrayPosition(Clip* clip); // public so menu can access it
	bool multiPadPressSelected;                         // public so menu can access it

	// called by melodic_instrument.cpp or kit.cpp
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) override;

	// called by playback_handler.cpp
	void notifyPlaybackBegun() override;

	// used to identify the UI as a clip UI or not.
	ClipMinder* toClipMinder() override { return getAutomationSubType() == AutomationSubType::ARRANGER ? NULL : this; }

	void setAutomationParamType();

	bool onAutomationOverview();
	bool inAutomationEditor();
	bool inNoteEditor();

	bool interpolation;
	bool interpolationBefore;
	bool interpolationAfter;

	// public to midi follow can access it
	ModelStackWithAutoParam*
	getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                              int32_t paramID = deluge::modulation::params::kNoParamID,
	                              deluge::modulation::params::Kind paramKind = deluge::modulation::params::Kind::NONE);

	// public so instrument clip view can access it
	void initParameterSelection(bool updateDisplay = true);
	bool onArrangerView;

	// public so uiTimerManager can access it
	void blinkInterpolationShortcut();
	void blinkPadSelectionShortcut();

	// public so menu can access it
	bool onMenuView;
	UI* previousUI; // previous UI so you can swap back UI after exiting menu
	int32_t getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t pos);
	void setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
	                                      int32_t knobPosRight);
	void resetInterpolationShortcutBlinking();
	void resetPadSelectionShortcutBlinking();
	AutomationParamType automationParamType;
	bool getAffectEntire() override;

	// public so action logger can access it
	void resetShortcutBlinking();

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
	void handleAffectEntireButtonAction(bool on);

	// edit pad action
	ActionResult handleEditPadAction(ModelStackWithAutoParam* modelStackWithParam,
	                                 ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, Clip* clip,
	                                 Output* output, OutputType outputType, int32_t effectiveLength, int32_t x,
	                                 int32_t y, int32_t velocity, SquareInfo& squareInfo);
	bool shortcutPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, Output* output,
	                       OutputType outputType, int32_t effectiveLength, int32_t x, int32_t y, int32_t velocity,
	                       int32_t xScroll, int32_t xZoom, SquareInfo& squareInfo);
	bool toggleAutomationInterpolation();
	bool toggleVelocityPadSelectionMode(SquareInfo& squareInfo);
	bool toggleAutomationPadSelectionMode(ModelStackWithAutoParam* modelStackWithParam, int32_t effectiveLength,
	                                      int32_t xScroll, int32_t xZoom);
	void handleParameterSelection(Clip* clip, Output* output, OutputType outputType, int32_t xDisplay,
	                              int32_t yDisplay);
	void noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, InstrumentClip* clip,
	                       int32_t x, int32_t y, int32_t velocity, int32_t effectiveLength, SquareInfo& squareInfo);
	void velocityPadSelectionAction(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip, int32_t x,
	                                int32_t y, int32_t velocity, SquareInfo& squareInfo);
	void velocityEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, InstrumentClip* clip,
	                           int32_t x, int32_t y, int32_t velocity, int32_t effectiveLength, SquareInfo& squareInfo);
	int32_t getVelocityFromY(int32_t y);
	int32_t getYFromVelocity(int32_t velocity);
	void addNoteWithNewVelocity(int32_t x, int32_t velocity, int32_t newVelocity);
	void adjustNoteVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x, int32_t velocity,
	                        int32_t newVelocity, uint8_t squareType);
	void setVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x, int32_t newVelocity);
	void setVelocityRamp(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
	                     SquareInfo rowSquareInfo[kDisplayWidth], int32_t velocityIncrement);
	void recordNoteEditPadAction(int32_t x, int32_t velocity);
	void automationEditPadAction(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xDisplay,
	                             int32_t yDisplay, int32_t velocity, int32_t effectiveLength, int32_t xScroll,
	                             int32_t xZoom);
	bool recordAutomationSinglePadPress(int32_t xDisplay, int32_t yDisplay);

	// mute pad action
	ActionResult handleMutePadAction(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                 InstrumentClip* instrumentClip, Output* output, OutputType outputType, int32_t y,
	                                 int32_t velocity);

	// audition pad action
	ActionResult handleAuditionPadAction(InstrumentClip* instrumentClip, Output* output, OutputType outputType,
	                                     int32_t y, int32_t velocity);
	void auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown);

	// Automation View Render Functions
	void performActualRender(RGB image[][kDisplayWidth + kSideBarWidth],
	                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll, uint32_t xZoom,
	                         int32_t renderWidth, int32_t imageWidth, bool drawUndefinedArea = true);
	void renderAutomationOverview(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                              ModelStackWithThreeMainThings* modelStackWithThreeMainThings, Clip* clip,
	                              OutputType outputType, RGB image[][kDisplayWidth + kSideBarWidth],
	                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
	                              bool isMIDICVDrum);
	void renderAutomationEditor(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                            RGB image[][kDisplayWidth + kSideBarWidth],
	                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth,
	                            int32_t xScroll, uint32_t xZoom, int32_t effectiveLength, int32_t xDisplay,
	                            bool drawUndefinedArea, deluge::modulation::params::Kind kind, bool isBipolar);
	void renderAutomationColumn(ModelStackWithAutoParam* modelStackWithParam,
	                            RGB image[][kDisplayWidth + kSideBarWidth],
	                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t lengthToDisplay,
	                            int32_t xDisplay, bool isAutomated, int32_t xScroll, int32_t xZoom,
	                            deluge::modulation::params::Kind kind, bool isBipolar);
	void renderAutomationBipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
	                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
	                                   int32_t yDisplay, bool isAutomated, deluge::modulation::params::Kind kind,
	                                   int32_t knobPos);
	void renderAutomationUnipolarSquare(RGB image[][kDisplayWidth + kSideBarWidth],
	                                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
	                                    int32_t yDisplay, bool isAutomated, int32_t knobPos);
	void renderNoteEditor(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
	                      RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth, int32_t xScroll,
	                      uint32_t xZoom, int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea,
	                      SquareInfo& squareInfo);
	void renderNoteColumn(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
	                      RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay, int32_t xScroll,
	                      int32_t xZoom, SquareInfo& squareInfo);
	void renderNoteSquare(RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay, int32_t yDisplay,
	                      uint8_t squareType, int32_t value);
	void renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay,
	                         RGB image[][kDisplayWidth + kSideBarWidth],
	                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t imageWidth,
	                         TimelineView* timelineView, bool tripletsOnHere, int32_t xDisplay);
	void renderDisplayOLED(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       int32_t knobPosRight = kNoSelection);
	void renderAutomationOverviewDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, Output* output,
	                                         OutputType outputType);
	void renderAutomationEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, Clip* clip,
	                                       OutputType outputType, int32_t knobPosLeft, int32_t knobPosRight);
	void renderNoteEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, InstrumentClip* clip,
	                                 OutputType outputType, int32_t knobPosLeft, int32_t knobPosRight);
	void renderDisplay7SEG(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       bool modEncoderAction = false);
	void renderAutomationOverviewDisplay7SEG(Output* output, OutputType outputType);
	void renderAutomationEditorDisplay7SEG(Clip* clip, OutputType outputType, int32_t knobPosLeft,
	                                       bool modEncoderAction);
	void renderNoteEditorDisplay7SEG(InstrumentClip* clip, OutputType outputType, int32_t knobPosLeft);

	// Enter/Exit Scale Mode
	void enterScaleMode(uint8_t yDisplay = 255);
	void exitScaleMode();

	// Horizontal Encoder Action
	void shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
	                                 int32_t effectiveLength);

	// Mod Encoder Action
	bool automationModEncoderActionForSelectedPad(ModelStackWithAutoParam* modelStackWithParam, int32_t whichModEncoder,
	                                              int32_t offset, int32_t effectiveLength);
	void automationModEncoderActionForUnselectedPad(ModelStackWithAutoParam* modelStackWithParam,
	                                                int32_t whichModEncoder, int32_t offset, int32_t effectiveLength);

	// Mod Encoder Button Action
	void copyAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xScroll, int32_t xZoom);
	void pasteAutomation(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t effectiveLength,
	                     int32_t xScroll, int32_t xZoom);

	// Select Encoder Action
	void selectGlobalParam(int32_t offset, Clip* clip);
	void selectNonGlobalParam(int32_t offset, Clip* clip);
	bool selectPatchCable(int32_t offset, Clip* clip);
	bool selectPatchCableAtIndex(Clip* clip, PatchCableSet* set, int32_t patchCableIndex, bool& foundCurrentPatchCable);
	void selectMIDICC(int32_t offset, Clip* clip);
	int32_t getNextSelectedParamArrayPosition(int32_t offset, int32_t lastSelectedParamArrayPosition,
	                                          int32_t numParams);
	void getLastSelectedNonGlobalParamArrayPosition(Clip* clip);
	void getLastSelectedGlobalParamArrayPosition(Clip* clip);

	// Automation Lanes Functions
	void initPadSelection();
	void initInterpolation();
	ParamManagerForTimeline* getParamManagerForClip(Clip* clip);
	int32_t getEffectiveLength(ModelStackWithTimelineCounter* modelStack);
	uint32_t getSquareWidth(int32_t square, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	uint32_t getMiddlePosFromSquare(int32_t xDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);

	void getAutomationParameterName(Clip* clip, OutputType outputType, StringBuf& parameterName);

	bool getAutomationNodeInterpolation(ModelStackWithAutoParam* modelStack, int32_t pos, bool reversed);
	void setAutomationParameterValue(ModelStackWithAutoParam* modelStack, int32_t knobPos, int32_t squareStart,
	                                 int32_t xDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                 bool modEncoderAction = false);
	void updateAutomationModPosition(ModelStackWithAutoParam* modelStack, uint32_t squareStart,
	                                 bool updateDisplay = true, bool updateIndicatorLevels = true);

	void handleAutomationSinglePadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t xDisplay,
	                                    int32_t yDisplay, int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	void handleAutomationParameterChange(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                                     OutputType outputType, int32_t xDisplay, int32_t yDisplay,
	                                     int32_t effectiveLength, int32_t xScroll, int32_t xZoom);
	int32_t calculateAutomationKnobPosForPadPress(ModelStackWithAutoParam* modelStackWithParam, OutputType outputType,
	                                              int32_t yDisplay);
	int32_t calculateAutomationKnobPosForMiddlePadPress(deluge::modulation::params::Kind kind, int32_t yDisplay);
	int32_t calculateAutomationKnobPosForSinglePadPress(deluge::modulation::params::Kind kind, int32_t yDisplay);

	void handleAutomationMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip, int32_t firstPadX,
	                                   int32_t firstPadY, int32_t secondPadX, int32_t secondPadY,
	                                   int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                   bool modEncoderAction = false);
	void renderAutomationDisplayForMultiPadPress(ModelStackWithAutoParam* modelStackWithParam, Clip* clip,
	                                             int32_t effectiveLength, int32_t xScroll, int32_t xZoom,
	                                             int32_t xDisplay = kNoSelection, bool modEncoderAction = false);

	int32_t calculateAutomationKnobPosForModEncoderTurn(ModelStackWithAutoParam* modelStackWithParam, int32_t knobPos,
	                                                    int32_t offset);

	void blinkShortcuts();
	void resetParameterShortcutBlinking();

	bool parameterShortcutBlinking;

	bool interpolationShortcutBlinking;
	bool padSelectionShortcutBlinking;

	bool padSelectionOn;
	bool multiPadPressActive;
	bool middlePadPressSelected;
	int32_t leftPadSelectedX;
	int32_t leftPadSelectedY;
	int32_t rightPadSelectedX;
	int32_t rightPadSelectedY;
	int32_t lastPadSelectedKnobPos;
	int32_t numNotesSelected;
	int32_t selectedPadPressed;

	bool playbackStopped;

	// grid sized array to assign midi cc values to each pad on the grid
	void initMIDICCShortcutsForAutomation();
	uint32_t midiCCShortcutsForAutomation[kDisplayWidth][kDisplayHeight];
	bool midiCCShortcutsLoaded;

	bool probabilityChanged;
	uint32_t timeSelectKnobLastReleased;
};

extern AutomationView automationView;
