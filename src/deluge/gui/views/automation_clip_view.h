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
#include "modulation/automation/copied_param_automation.h"
#include "modulation/params/param_node.h"

class InstrumentClip;
class NoteRow;
class Note;
class Editor;
class Instrument;
class Sound;
class ModControllable;
class Drum;
class SoundDrum;
class ParamManagerForTimeline;
class Action;
class ParamCollection;
class CopiedNoteRow;
class ParamNode;
class ModelStackWithTimelineCounter;
class ModelStackWithNoteRow;
class MidiInstrument;
class ModelStackWithAutoParam;
class ModelStackWithThreeMainThings;

struct EditAutomationPadPress {
	bool isActive;
	uint8_t yDisplay;
	uint8_t xDisplay;
	bool deleteOnDepress; // Can also mean to delete tail
	uint8_t intendedVelocity;
	uint8_t intendedProbability;
	bool deleteOnScroll;
	bool isBlurredSquare;
	bool mpeCachedYet;
	StolenParamNodes stolenMPE[kNumExpressionDimensions];
	uint32_t intendedPos;    // For "blurred squares", means start of square
	uint32_t intendedLength; // For "blurred squares", means length of square
};

#define MPE_RECORD_LENGTH_FOR_NOTE_EDITING 3
#define MPE_RECORD_INTERVAL_TIME (44100 >> 2) // 250ms

class AutomationClipView final : public ClipView, public InstrumentClipMinder {
public:
	AutomationClipView();
	bool opened();
	void openedInBackground();
	void focusRegained();

	//called by ui_timer_manager - might need to revise this routine for automation clip view since it references notes
	void graphicsRoutine();

	//rendering
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	void performActualRender(uint32_t whichRows, uint8_t* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                         int32_t xScroll, uint32_t xZoom, int renderWidth, int imageWidth,
	                         bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void drawAuditionSquare(uint8_t yDisplay, uint8_t thisImage[][3]);

	void recalculateColours();
	void recalculateColour(uint8_t yDisplay);

	inline void getRowColour(int y, uint8_t color[3]) {
		color[0] = rowColour[y][0];
		color[1] = rowColour[y][1];
		color[2] = rowColour[y][2];
	}

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
		InstrumentClipMinder::renderOLED(image);
	}
#endif

	//button action
	ActionResult buttonAction(hid::Button b, bool on, bool inCardRoutine);
	void createNewInstrument(InstrumentType instrumentType);
	void changeInstrumentType(InstrumentType newInstrumentType);
	void transitionToSessionView();
	void flashDefaultRootNote();
	void cancelAllAuditioning();

	//pad action
	ActionResult padAction(int x, int y, int velocity);
	void enterScaleMode(uint8_t yDisplay = 255);
	void exitScaleMode();
	void changeRootNote(uint8_t yDisplay);

	//edit pad action
	void editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, unsigned int xZoom);
	int16_t mpeValuesAtHighestPressure[MPE_RECORD_LENGTH_FOR_NOTE_EDITING][kNumExpressionDimensions];
	int16_t mpeMostRecentPressure;
	uint32_t mpeRecordLastUpdateTime;

	//mute pad press
	void mutePadPress(uint8_t yDisplay);

	//audition pad action
	void auditionPadAction(int velocity, int yDisplay, bool shiftButtonDown);
	ModelStackWithNoteRow* createNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack, int yDisplay);
	NoteRow* createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, int yDisplay, int* getIndex = NULL);
	void reassessAllAuditionStatus();
	void reassessAuditionStatus(uint8_t yDisplay);
	uint8_t getVelocityForAudition(uint8_t yDisplay, uint32_t* sampleSyncLength);
	uint8_t oneNoteAuditioning();
	uint8_t getNumNoteRowsAuditioning();
	inline void sendAuditionNote(bool on, uint8_t yDisplay) { sendAuditionNote(on, yDisplay, 64, 0); };
	bool
	    auditioningSilently; // Sometimes the user will want to hold an audition pad without actually sounding the note, by holding an encoder
	bool fileBrowserShouldNotPreview; // Archaic leftover feature that users wouldn't let me get rid of

	//horizontal encoder action
	ActionResult horizontalEncoderAction(int offset);
	void doubleClipLengthAction();
	ModelStackWithNoteRow* getOrCreateNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack, int yDisplay);

	//vertical encoder action
	ActionResult verticalEncoderAction(int offset, bool inCardRoutine);
	ActionResult scrollVertical(int scrollAmount, bool inCardRoutine, bool shiftingNoteRow = false);

	//mod encoder action
	void modEncoderAction(int whichModEncoder, int offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	void dontDeleteNotesOnDepress();
	CopiedParamAutomation copiedParamAutomation;

	//tempo encoder action
	void tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed);

	//Select encoder action
	void selectEncoderAction(int8_t offset);
	void offsetNoteCodeAction(int newOffset);
	void cutAuditionedNotesToOne();
	void setSelectedDrum(Drum* drum, bool shouldRedrawStuff = true);

	//called by melodic_instrument.cpp or kit.cpp
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow);

	//called by playback_handler.cpp
	void notifyPlaybackBegun();

	//called by sound_drum.cpp
	bool isDrumAuditioned(Drum* drum);

	//not sure how this is used
	ClipMinder* toClipMinder() { return this; }

	//Automation Lanes Global Variables / Toggles
	uint8_t clipClear;
	uint8_t drawLine;
	uint8_t flashShortcuts;
	uint8_t notePassthrough;
	uint8_t overlayNotes;
	uint8_t interpolateOn;

private:

	//Rendering Variables
	void setLedStates();
	uint8_t rowColour[kDisplayHeight][3];
	uint8_t rowTailColour[kDisplayHeight][3];
	uint8_t rowBlurColour[kDisplayHeight][3];
	uint8_t yDisplayOfNewNoteRow;

	//Pad Action Variables
	void endEditPadPress(uint8_t i);
	void checkIfAllEditPadPressesEnded(bool mayRenderSidebar = true);
	uint8_t numEditPadPressesPerNoteRowOnScreen[kDisplayHeight];
	EditAutomationPadPress editPadPresses[kEditPadPressBufferSize];
	uint8_t numEditPadPresses;
	uint32_t timeLastEditPadPress;
	uint32_t timeFirstEditPadPress;

	//Audition Pad Action Variables
	void sendAuditionNote(bool on, uint8_t yDisplay, uint8_t velocity, uint32_t sampleSyncLength);
	void someAuditioningHasEnded(bool recalculateLastAuditionedNoteOnScreen);
	uint8_t lastAuditionedYDisplay;
	uint8_t lastAuditionedVelocityOnScreen[kDisplayHeight]; // 255 seems to mean none
	uint8_t auditionPadIsPressed[kDisplayHeight];
	bool
	    editedAnyPerNoteRowStuffSinceAuditioningBegan; // Because in this case we can assume that if they press a main pad while auditioning, they're not intending to do that shortcut into the SoundEditor!
	Drum* drumForNewNoteRow;

	//Horizontal Encoder Action
	void rotateNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int offset, int yDisplay,
	                               bool shouldDisplayDirectionEvenIfNoNoteRow = false);
	void editNoteRowLength(ModelStackWithNoteRow* modelStack, int offset, int yDisplay);
	void nudgeNotes(int offset);
	bool offsettingNudgeNumberDisplay;
	bool doneAnyNudgingSinceFirstEditPadPress;
	bool shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress;
	uint32_t
	    timeHorizontalKnobLastReleased; // Only to be looked at if shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress is true after they rotated a NoteRow and might now be wanting to instead edit its length after releasing the knob

	//Vertical Encoder Action
	bool getAffectEntire();
	bool shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress;

	//Mod Encoder Action
	void copyAutomation(int whichModEncoder);
	void pasteAutomation(int whichModEncoder);

	//Automation Lanes Functions
	ModelStackWithAutoParam* getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip, int32_t paramID = 0xFFFFFFFF);
	void setParameterAutomationValue(ModelStackWithAutoParam* modelStack, int32_t knobPos, int32_t squareStart,
	                                 int32_t xDisplay, int32_t effectiveLength);

	void handleSinglePadPress(ModelStackWithTimelineCounter* modelStack, Clip* clip, int32_t xDisplay,
	                          int32_t yDisplay);
	int calculateKnobPosForSinglePadPress(int32_t yDisplay);

	void handleMultiPadPress(ModelStackWithTimelineCounter* modelStack, Clip* clip, int32_t firstPadX,
	                         int32_t firstPadY, int32_t secondPadX, int32_t secondPadY);
	int calculateKnobPosForMultiPadPress(int32_t xDisplay, int32_t firstPadX, int32_t firstPadValue, int32_t secondPadX,
	                                     int32_t secondPadValue);

	int calculateKnobPosForModEncoderTurn(int32_t knobPos, int32_t offset);

	//Interpolation Shape Functions
	int LERP(int A, int B, int T, int Distance);
	int LERPRoot(int A, int B, int T, int Distance);
	int LERPSweep(int A, int B, int T, int Distance);
	int LERPSweepDown(int A, int B, int T, int Distance);
	//int smoothstep (int A, int B, int T, int Distance);

	uint8_t lastSelectedParamID;
	uint8_t lastSelectedParamX;
	uint8_t lastSelectedParamY;
	uint8_t lastSelectedParamArrayPosition;
	uint8_t lastSelectedMidiCC;
	uint8_t lastSelectedMidiX;
	uint8_t lastSelectedMidiY;
	uint8_t selectedParamColour[kDisplayHeight][3];
	uint8_t lastEditPadPressXDisplay;
};

extern AutomationClipView automationClipView;
