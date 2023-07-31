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

struct EditPadPress {
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

#define NUDGEMODE_QUANTIZE 1
#define NUDGEMODE_QUANTIZE_ALL 2

class InstrumentClipView final : public ClipView, public InstrumentClipMinder {
public:
	InstrumentClipView();
	bool opened();
	void focusRegained();
	ActionResult buttonAction(hid::Button b, bool on, bool inCardRoutine);
	ActionResult padAction(int x, int y, int velocity);
	uint8_t getEditPadPressXDisplayOnScreen(uint8_t yDisplay);
	void editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, unsigned int xZoom);
	void adjustVelocity(int velocityChange);
	void mutePadPress(uint8_t yDisplay);
	bool ensureNoteRowExistsForYDisplay(uint8_t yDisplay);
	void recalculateColours();
	void recalculateColour(uint8_t yDisplay);
	ActionResult scrollVertical(int scrollAmount, bool inCardRoutine, bool shiftingNoteRow = false);
	void reassessAllAuditionStatus();
	void reassessAuditionStatus(uint8_t yDisplay);
	uint8_t getVelocityForAudition(uint8_t yDisplay, uint32_t* sampleSyncLength);
	uint8_t getNumNoteRowsAuditioning();
	uint8_t oneNoteAuditioning();
	void offsetNoteCodeAction(int newOffset);
	int getYVisualFromYDisplay(int yDisplay);
	int getYVisualWithinOctaveFromYDisplay(int yDisplay);
	void auditionPadAction(int velocity, int yDisplay, bool shiftButtonDown);
	void enterScaleMode(uint8_t yDisplay = 255);
	void exitScaleMode();
	void changeRootNote(uint8_t yDisplay);
	void drawMuteSquare(NoteRow* thisNoteRow, uint8_t thisImage[][3], uint8_t thisOccupancyMask[]);
	void cutAuditionedNotesToOne();
	ActionResult verticalEncoderAction(int offset, bool inCardRoutine);
	ActionResult horizontalEncoderAction(int offset);
	void fillOffScreenImageStores();
	void graphicsRoutine();

	void drawAuditionSquare(uint8_t yDisplay, uint8_t thisImage[][3]);
	void flashDefaultRootNote();
	void selectEncoderAction(int8_t offset);
	void doubleClipLengthAction();
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow);
	void setSelectedDrum(Drum* drum, bool shouldRedrawStuff = true);
	bool isDrumAuditioned(Drum* drum);
	int setupForEnteringScaleMode(int newRootNote = 2147483647, int yDisplay = (kDisplayHeight / 2));
	int setupForExitingScaleMode();
	void setupChangingOfRootNote(int newRootNote, int yDisplay = (kDisplayHeight / 2));
	void deleteDrum(SoundDrum* drum);
	void cancelAllAuditioning();
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);

	void tellMatrixDriverWhichRowsContainSomethingZoomable();
	void drawDrumName(Drum* drum, bool justPopUp = false);
	void notifyPlaybackBegun();
	void openedInBackground();
	bool renderMainPads(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true);
	void performActualRender(uint32_t whichRows, uint8_t* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                         int32_t xScroll, uint32_t xZoom, int renderWidth, int imageWidth,
	                         bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, uint8_t image[][kDisplayWidth + kSideBarWidth][3],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

	void transitionToSessionView();
	void playbackEnded();
	void scrollFinished();
	void clipNeedsReRendering(Clip* clip);
	void modEncoderAction(int whichModEncoder, int offset);
	ClipMinder* toClipMinder() { return this; }
	void reportMPEInitialValuesForNoteEditing(ModelStackWithNoteRow* modelStack, int16_t const* mpeValues);
	void reportMPEValueForNoteEditing(int whichExpressionDimension, int32_t value);
	void reportNoteOffForMPEEditing(ModelStackWithNoteRow* modelStack);
	void dontDeleteNotesOnDepress();

	void tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed);

	//made these public so they can be accessed by the automation clip view
	void sendAuditionNote(bool on, uint8_t yDisplay, uint8_t velocity, uint32_t sampleSyncLength);
	void setLedStates();
	uint32_t getSquareWidth(int32_t square, int32_t effectiveLength);
	void drawNoteCode(uint8_t yDisplay);
	void createNewInstrument(InstrumentType instrumentType);
	void changeInstrumentType(InstrumentType newInstrumentType);
	Drum* flipThroughAvailableDrums(int newOffset, Drum* drum, bool mayBeNone = false);
	void enterDrumCreator(ModelStackWithNoteRow* modelStack, bool doRecording = false);
	NoteRow* createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, int yDisplay, int* getIndex = NULL);
	Sound* getSoundForNoteRow(NoteRow* noteRow, ParamManagerForTimeline** getParamManager);
	ModelStackWithNoteRow* createNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack, int yDisplay);
	ModelStackWithNoteRow* getOrCreateNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack, int yDisplay);
	void editNoteRowLength(ModelStackWithNoteRow* modelStack, int offset, int yDisplay);
	void someAuditioningHasEnded(bool recalculateLastAuditionedNoteOnScreen);
	void checkIfAllEditPadPressesEnded(bool mayRenderSidebar = true);
	void endEditPadPress(uint8_t i);
	bool getAffectEntire();
	//made these public so they can be accessed by the automation clip view

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {
		InstrumentClipMinder::renderOLED(image);
	}
#endif

	CopiedNoteRow* firstCopiedNoteRow;
	int32_t copiedScreenWidth;
	ScaleType copiedScaleType;
	int16_t copiedYNoteOfBottomRow;

	CopiedParamAutomation copiedParamAutomation;
	bool
	    auditioningSilently; // Sometimes the user will want to hold an audition pad without actually sounding the note, by holding an encoder
	bool fileBrowserShouldNotPreview; // Archaic leftover feature that users wouldn't let me get rid of

	int16_t mpeValuesAtHighestPressure[MPE_RECORD_LENGTH_FOR_NOTE_EDITING][kNumExpressionDimensions];
	int16_t mpeMostRecentPressure;
	uint32_t mpeRecordLastUpdateTime;

	//made these public so they can be accessed by the automation clip view
	EditPadPress editPadPresses[kEditPadPressBufferSize];
	uint8_t lastAuditionedVelocityOnScreen[kDisplayHeight]; // 255 seems to mean none
	uint8_t auditionPadIsPressed[kDisplayHeight];
	uint8_t rowColour[kDisplayHeight][3];
	uint8_t rowTailColour[kDisplayHeight][3];
	uint8_t rowBlurColour[kDisplayHeight][3];
	uint8_t numEditPadPressesPerNoteRowOnScreen[kDisplayHeight];
	uint8_t lastAuditionedYDisplay;
	uint8_t numEditPadPresses;
	uint32_t timeLastEditPadPress;
	uint32_t timeFirstEditPadPress;
	uint32_t
	    timeHorizontalKnobLastReleased; // Only to be looked at if shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress is true after they rotated a NoteRow and might now be wanting to instead edit its length after releasing the knob
	bool shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress;
	bool shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress;
	//made these public so they can be accessed by the automation clip view

private:

	bool doneAnyNudgingSinceFirstEditPadPress;
	bool offsettingNudgeNumberDisplay;
	bool
	    editedAnyPerNoteRowStuffSinceAuditioningBegan; // Because in this case we can assume that if they press a main pad while auditioning, they're not intending to do that shortcut into the SoundEditor!

	uint8_t flashScaleModeLedErrorCount;

	Drum* selectedDrum;

	Drum* drumForNewNoteRow;
	uint8_t yDisplayOfNewNoteRow;

	int32_t quantizeAmount;

	Drum* getNextDrum(Drum* oldDrum, bool mayBeNone = false);

	void adjustProbability(int offset);
	void copyNotes();
	void pasteNotes();
	void deleteCopiedNoteRows();

	void copyAutomation(int whichModEncoder);
	void pasteAutomation(int whichModEncoder);

	void createDrumForAuditionedNoteRow(DrumType drumType);
	void nudgeNotes(int offset);
	void editNoteRepeat(int offset);

	bool isRowAuditionedByInstrument(int yDisplay);

	void editNumEuclideanEvents(ModelStackWithNoteRow* modelStack, int offset, int yDisplay);
	void rotateNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int offset, int yDisplay,
	                               bool shouldDisplayDirectionEvenIfNoNoteRow = false);

	void quantizeNotes(int offset, int nudgeMode);
};

extern InstrumentClipView instrumentClipView;
