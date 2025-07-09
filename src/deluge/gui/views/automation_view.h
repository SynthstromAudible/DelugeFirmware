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
	UIType getUIContextType() override;
	UIModControllableContext getUIModControllableContext() override {
		return getUIContextType() == UIType::ARRANGER ? UIModControllableContext::SONG : UIModControllableContext::CLIP;
	}

	// used to identify the UI as a clip UI or not.
	ClipMinder* toClipMinder() override { return getUIContextType() == UIType::ARRANGER ? nullptr : this; }

	void setAutomationParamType();

	bool onAutomationOverview();
	bool inAutomationEditor();
	bool inNoteEditor();

	// rendering
	bool possiblyRefreshAutomationEditorGrid(Clip* clip, deluge::modulation::params::Kind paramKind, int32_t paramID);
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	void renderUndefinedArea(int32_t xScroll, uint32_t xZoom, int32_t lengthToDisplay,
	                         RGB image[][kDisplayWidth + kSideBarWidth],
	                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t imageWidth,
	                         TimelineView* timelineView, bool tripletsOnHere, int32_t xDisplay);
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
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
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

	bool interpolation;
	bool interpolationBefore;
	bool interpolationAfter;

	// public to midi follow and editor layouts can access it
	ModelStackWithAutoParam*
	getModelStackWithParamForClip(ModelStackWithTimelineCounter* modelStack, Clip* clip,
	                              int32_t paramID = deluge::modulation::params::kNoParamID,
	                              deluge::modulation::params::Kind paramKind = deluge::modulation::params::Kind::NONE);

	// public so instrument clip view can access it
	void initParameterSelection(bool updateDisplay = true);
	bool onArrangerView;

	// public so uiTimerManager and editor layouts can access it
	void blinkInterpolationShortcut();
	void blinkPadSelectionShortcut();

	// public so menu and editor layouts can access it
	bool onMenuView;
	UI* previousUI; // previous UI so you can swap back UI after exiting menu
	int32_t getAutomationParameterKnobPos(ModelStackWithAutoParam* modelStack, uint32_t pos);
	void setAutomationKnobIndicatorLevels(ModelStackWithAutoParam* modelStack, int32_t knobPosLeft,
	                                      int32_t knobPosRight);
	void initInterpolation();
	void resetInterpolationShortcutBlinking();
	void initPadSelection();
	void resetPadSelectionShortcutBlinking();

	AutomationParamType automationParamType;
	bool getAffectEntire() override;

	// public so action logger can access it
	void resetShortcutBlinking();

	bool padSelectionOn;
	bool multiPadPressActive;
	bool middlePadPressSelected;
	int32_t leftPadSelectedX;
	int32_t leftPadSelectedY;
	int32_t rightPadSelectedX;
	int32_t rightPadSelectedY;
	int32_t lastPadSelectedKnobPos;

private:
	// button action functions
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
	void handleParameterSelection(Clip* clip, Output* output, OutputType outputType, int32_t xDisplay,
	                              int32_t yDisplay);
	// mute pad action
	ActionResult handleMutePadAction(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                                 InstrumentClip* instrumentClip, Output* output, OutputType outputType, int32_t y,
	                                 int32_t velocity);

	// audition pad action
	ActionResult handleAuditionPadAction(InstrumentClip* instrumentClip, Output* output, OutputType outputType,
	                                     int32_t y, int32_t velocity);
	ActionResult auditionPadAction(InstrumentClip* clip, Output* output, OutputType outputType, int32_t yDisplay,
	                               int32_t velocity, bool shiftButtonDown);

	// Automation View Render Functions
	void performActualRender(RGB image[][kDisplayWidth + kSideBarWidth],
	                         uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xScroll, uint32_t xZoom,
	                         int32_t renderWidth, int32_t imageWidth, bool drawUndefinedArea = true);
	void renderAutomationOverview(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                              ModelStackWithThreeMainThings* modelStackWithThreeMainThings, Clip* clip,
	                              OutputType outputType, RGB image[][kDisplayWidth + kSideBarWidth],
	                              uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay,
	                              bool isMIDICVDrum);
	void renderDisplayOLED(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       int32_t knobPosRight = kNoSelection);
	void renderAutomationOverviewDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, Output* output,
	                                         OutputType outputType);
	void renderDisplay7SEG(Clip* clip, Output* output, OutputType outputType, int32_t knobPosLeft = kNoSelection,
	                       bool modEncoderAction = false);
	void renderAutomationOverviewDisplay7SEG(Output* output, OutputType outputType);

	// Horizontal Encoder Action
	void shiftAutomationHorizontally(ModelStackWithAutoParam* modelStackWithParam, int32_t offset,
	                                 int32_t effectiveLength);

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

	void blinkShortcuts();
	void resetParameterShortcutBlinking();
	bool parameterShortcutBlinking;
	bool interpolationShortcutBlinking;
	bool padSelectionShortcutBlinking;

	int32_t getEffectiveLength(ModelStackWithTimelineCounter* modelStack);

	// grid sized array to assign midi cc values to each pad on the grid
	void initMIDICCShortcutsForAutomation();
	uint32_t midiCCShortcutsForAutomation[kDisplayWidth][kDisplayHeight];
	bool midiCCShortcutsLoaded;

	bool probabilityChanged;
	uint32_t timeSelectKnobLastReleased;
};

extern AutomationView automationView;
