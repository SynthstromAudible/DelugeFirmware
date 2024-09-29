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
#include "model/note/note_row.h"
#include "modulation/automation/copied_param_automation.h"
#include "modulation/params/param_node.h"
#include "util/d_string.h"

class InstrumentClip;
class NoteRow;
class Note;
class Editor;
class Instrument;
class Sound;
class ModControllable;
class Kit;
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
	uint8_t intendedIterance;
	uint8_t intendedFill;
	bool deleteOnScroll;
	bool isBlurredSquare;
	bool mpeCachedYet;
	StolenParamNodes stolenMPE[kNumExpressionDimensions];
	uint32_t intendedPos;    // For "blurred squares", means start of square
	uint32_t intendedLength; // For "blurred squares", means length of square
};

#define MPE_RECORD_LENGTH_FOR_NOTE_EDITING 3
#define MPE_RECORD_INTERVAL_TIME (kSampleRate >> 2) // 250ms

enum class NudgeMode { QUANTIZE, QUANTIZE_ALL };

class InstrumentClipView final : public ClipView, public InstrumentClipMinder {
public:
	InstrumentClipView();
	bool opened() override;
	void focusRegained() override;
	void displayOrLanguageChanged() final;
	const char* getName() { return "instrument_clip_view"; }
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;

	// SCALE MODE related commands.

	/// Learn notes from current clip + scale mode clips as USER scale.
	/// Enters scale mode if wasn't in it already. LEARN + SCALE.
	ActionResult commandLearnUserScale();
	/// Cycle through preset scales. SHIFT + SCALE in scale mode.
	ActionResult commandCycleThroughScales();
	/// Flash the current root note. Part of SCALE down.
	ActionResult commandFlashRootNote();
	/// Enter scale mode with selected root. AUDITION + SCALE or SCALE + AUDITION in schromatic mode.
	ActionResult commandEnterScaleModeWithRoot(uint8_t root);
	/// Enter scale mode. SCALE when in chromatic mode.
	ActionResult commandEnterScaleMode();
	/// Exit scale mdoe. SCALE when in scale mode.
	ActionResult commandExitScaleMode();
	/// Change current root note. AUDITION + SCALE or SCALE + AUDITION in scale mod.
	ActionResult commandChangeRootNote(uint8_t yDisplay);

	uint8_t getEditPadPressXDisplayOnScreen(uint8_t yDisplay);
	void editPadAction(bool state, uint8_t yDisplay, uint8_t xDisplay, uint32_t xZoom);
	void mutePadPress(uint8_t yDisplay);
	bool ensureNoteRowExistsForYDisplay(uint8_t yDisplay);
	void recalculateColours();
	void recalculateColour(uint8_t yDisplay);
	ActionResult scrollVertical(int32_t scrollAmount, bool inCardRoutine, bool shiftingNoteRow = false);
	void reassessAllAuditionStatus();
	void reassessAuditionStatus(uint8_t yDisplay);
	uint8_t getVelocityForAudition(uint8_t yDisplay, uint32_t* sampleSyncLength);
	uint8_t getNumNoteRowsAuditioning();
	uint8_t oneNoteAuditioning();
	void offsetNoteCodeAction(int32_t newOffset);
	int32_t getYVisualFromYDisplay(int32_t yDisplay);
	int32_t getYVisualWithinOctaveFromYDisplay(int32_t yDisplay);
	ActionResult auditionPadAction(int32_t velocity, int32_t yDisplay, bool shiftButtonDown);
	void potentiallyRefreshNoteRowMenu();
	void enterScaleMode(uint8_t yDisplay = 255);
	void exitScaleMode();
	void drawMuteSquare(NoteRow* thisNoteRow, RGB thisImage[], uint8_t thisOccupancyMask[]);
	void cutAuditionedNotesToOne();
	ActionResult verticalEncoderAction(int32_t offset, bool inCardRoutine) override;
	ActionResult horizontalEncoderAction(int32_t offset) override;
	void fillOffScreenImageStores();
	void graphicsRoutine() override;

	void drawAuditionSquare(uint8_t yDisplay, RGB thisImage[]);
	void flashDefaultRootNote();
	void selectEncoderAction(int8_t offset) override;
	void doubleClipLengthAction();
	void setSelectedDrum(Drum* drum, bool shouldRedrawStuff = true, Kit* selectedKit = nullptr,
	                     bool shouldSendMidiFeedback = true);
	bool isDrumAuditioned(Drum* drum);
	int32_t setupForEnteringScaleMode(int32_t newRootNote = 2147483647, int32_t yDisplay = (kDisplayHeight / 2));
	int32_t setupForExitingScaleMode();
	void setupChangingOfRootNote(int32_t newRootNote, int32_t yDisplay = (kDisplayHeight / 2));
	void deleteDrum(SoundDrum* drum);
	void cancelAllAuditioning();
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on) override;

	void tellMatrixDriverWhichRowsContainSomethingZoomable() override;
	void drawDrumName(Drum* drum, bool justPopUp = false);
	void getDrumName(Drum* drum, StringBuf& drumName);
	void notifyPlaybackBegun() override;
	void openedInBackground();
	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea = true) override;
	void performActualRender(uint32_t whichRows, RGB* image, uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
	                         int32_t xScroll, uint32_t xZoom, int32_t renderWidth, int32_t imageWidth,
	                         bool drawUndefinedArea = true);
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;

	void playbackEnded() override;
	void scrollFinished() override;
	void clipNeedsReRendering(Clip* clip) override;
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override;
	ClipMinder* toClipMinder() override { return this; }
	void reportMPEInitialValuesForNoteEditing(ModelStackWithNoteRow* modelStack, int16_t const* mpeValues);
	void reportMPEValueForNoteEditing(int32_t whichExpressionDimension, int32_t value);
	void reportNoteOffForMPEEditing(ModelStackWithNoteRow* modelStack);
	void dontDeleteNotesOnDepress();

	void tempoEncoderAction(int8_t offset, bool encoderButtonPressed, bool shiftButtonPressed);
	void sendAuditionNote(bool on, uint8_t yDisplay, uint8_t velocity, uint32_t sampleSyncLength);

	// made these public so they can be accessed by the automation clip view
	void setLedStates();
	uint32_t getSquareWidth(int32_t square, int32_t effectiveLength);
	void drawNoteCode(uint8_t yDisplay);
	void createNewInstrument(OutputType instrumentType, bool is_fm = false);
	void changeOutputType(OutputType newOutputType);
	Sound* getSoundForNoteRow(NoteRow* noteRow, ParamManagerForTimeline** getParamManager);
	void someAuditioningHasEnded(bool recalculateLastAuditionedNoteOnScreen);
	bool getAffectEntire() override;
	void checkIfAllEditPadPressesEnded(bool mayRenderSidebar = true);
	void endEditPadPress(uint8_t i);
	void endAllEditPadPresses();
	void copyAutomation(int32_t whichModEncoder, int32_t navSysId = NAVIGATION_CLIP);
	void pasteAutomation(int32_t whichModEncoder, int32_t navSysId = NAVIGATION_CLIP);
	// made these public so they can be accessed by the automation clip view

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override {
		InstrumentClipMinder::renderOLED(canvas);
	}

	CopiedParamAutomation copiedParamAutomation;
	// Sometimes the user will want to hold an audition pad without actually sounding the note, by holding an encoder
	bool auditioningSilently;
	// Archaic leftover feature that users wouldn't let me get rid of
	bool fileBrowserShouldNotPreview;

	int16_t mpeValuesAtHighestPressure[MPE_RECORD_LENGTH_FOR_NOTE_EDITING][kNumExpressionDimensions];
	int16_t mpeMostRecentPressure;
	uint32_t mpeRecordLastUpdateTime;

	// made these public so they can be accessed by the automation clip view
	EditPadPress editPadPresses[kEditPadPressBufferSize];
	uint8_t lastAuditionedVelocityOnScreen[kDisplayHeight]; // 255 seems to mean none
	uint8_t auditionPadIsPressed[kDisplayHeight];
	uint8_t numEditPadPressesPerNoteRowOnScreen[kDisplayHeight];
	uint8_t lastAuditionedYDisplay;
	uint8_t numEditPadPresses;
	uint32_t timeLastEditPadPress;
	uint32_t timeFirstEditPadPress;
	// Only to be looked at if shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress is true after they
	// rotated a NoteRow and might now be wanting to instead edit its length after releasing the knob
	uint32_t timeHorizontalKnobLastReleased;
	bool shouldIgnoreVerticalScrollKnobActionIfNotAlsoPressedForThisNotePress;
	bool shouldIgnoreHorizontalScrollKnobActionIfNotAlsoPressedForThisNotePress;
	// Because in this case we can assume that if they press a main pad while auditioning, they're not intending to do
	// that shortcut into the SoundEditor!
	bool editedAnyPerNoteRowStuffSinceAuditioningBegan;
	// made these public so they can be accessed by the automation clip view

	// ui
	UIType getUIType() override { return UIType::INSTRUMENT_CLIP; }

	// note editor
	bool enterNoteEditor();
	void exitNoteEditor();
	void handleNoteEditorEditPadAction(int32_t x, int32_t y, int32_t on);
	void deselectNoteAndGoUpOneLevel();
	ActionResult handleNoteEditorVerticalEncoderAction(int32_t offset, bool inCardRoutine);
	ActionResult handleNoteEditorHorizontalEncoderAction(int32_t offset);
	ActionResult handleNoteEditorButtonAction(deluge::hid::Button b, bool on, bool inCardRoutine);

	SquareInfo gridSquareInfo[kDisplayHeight][kDisplayWidth];
	int32_t lastSelectedNoteXDisplay;
	int32_t lastSelectedNoteYDisplay;

	// adjust note parameters
	void adjustVelocity(int32_t velocityChange);
	void updateVelocityValue(int32_t& velocityValue, int32_t newVelocity);
	void displayVelocity(int32_t velocityValue, int32_t velocityChange);
	void popupVelocity(char const* displayString);

	void adjustNoteProbability(int32_t offset);
	void adjustNoteIterance(int32_t offset);
	void adjustNoteFill(int32_t offset);
	Note* getLeftMostNotePressed();
	void adjustNoteParameterValue(int32_t offset, int32_t changeType, int32_t parameterMinValue,
	                              int32_t parameterMaxValue);

	// other note functions
	void editNoteRepeat(int32_t offset);

	// blink selected note
	void blinkSelectedNote(int32_t whichMainRows = 0);
	void resetSelectedNoteBlinking();

	// note row editor
	bool enterNoteRowEditor();
	void exitNoteRowEditor();
	bool handleNoteRowEditorPadAction(int32_t x, int32_t y, int32_t on);
	bool handleNoteRowEditorMainPadAction(int32_t x, int32_t y, int32_t on);
	void handleNoteRowEditorAuditionPadAction(int32_t y);
	ActionResult handleNoteRowEditorVerticalEncoderAction(int32_t offset, bool inCardRoutine);
	ActionResult handleNoteRowEditorHorizontalEncoderAction(int32_t offset);
	ActionResult handleNoteRowEditorButtonAction(deluge::hid::Button b, bool on, bool inCardRoutine);

	// adjust note row parameters
	int32_t setNoteRowProbability(int32_t offset);
	int32_t setNoteRowIterance(int32_t offset);
	int32_t setNoteRowFill(int32_t offset);
	int32_t setNoteRowParameterValue(int32_t offset, int32_t changeType, int32_t parameterMinValue,
	                                 int32_t parameterMaxValue);

	// other note row functions
	ModelStackWithNoteRow* createNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack, int32_t yDisplay);
	ModelStackWithNoteRow* getOrCreateNoteRowForYDisplay(ModelStackWithTimelineCounter* modelStack, int32_t yDisplay);
	void editNumEuclideanEvents(ModelStackWithNoteRow* modelStack, int32_t offset, int32_t yDisplay);
	void editNoteRowLength(int32_t offset);
	void rotateNoteRowHorizontally(int32_t offset);
	void editNoteRowLength(ModelStackWithNoteRow* modelStack, int32_t offset, int32_t yDisplay);
	void noteRowChanged(InstrumentClip* clip, NoteRow* noteRow) override;

	// blink selected note row
	void blinkSelectedNoteRow(int32_t whichMainRows = 0);
	void resetSelectedNoteRowBlinking();
	bool noteRowFlashOn;
	bool noteRowBlinking;

	const char* getFillString(uint8_t fill);

	// public for velocity keyboard view to access
	void enterDrumCreator(ModelStackWithNoteRow* modelStack, bool doRecording = false);

private:
	bool doneAnyNudgingSinceFirstEditPadPress;
	bool offsettingNudgeNumberDisplay;

	uint8_t flashScaleModeLedErrorCount;

	Drum* selectedDrum;

	Drum* drumForNewNoteRow;
	uint8_t yDisplayOfNewNoteRow;

	int32_t quantizeAmount;
	uint32_t timeSongButtonPressed;

	std::array<RGB, kDisplayHeight> rowColour;
	std::array<RGB, kDisplayHeight> rowTailColour;
	std::array<RGB, kDisplayHeight> rowBlurColour;

	// note functions
	void nudgeNotes(int32_t offset);
	void displayProbability(uint8_t probability, bool prevBase);

	// note row functions
	void copyNotes();
	void pasteNotes(bool overwriteExisting);
	void deleteCopiedNoteRows();
	CopiedNoteRow* firstCopiedNoteRow;
	int32_t copiedScreenWidth;
	ScaleType copiedScaleType;
	int16_t copiedYNoteOfBottomRow;

	void rotateNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int32_t offset, int32_t yDisplay,
	                               bool shouldDisplayDirectionEvenIfNoNoteRow = false);

	Drum* getNextDrum(Drum* oldDrum, bool mayBeNone = false);
	Drum* flipThroughAvailableDrums(int32_t newOffset, Drum* drum, bool mayBeNone = false);
	NoteRow* createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, int32_t yDisplay,
	                                int32_t* getIndex = NULL);
	void createDrumForAuditionedNoteRow(DrumType drumType);
	bool isRowAuditionedByInstrument(int32_t yDisplay);

	// TEMPO encoder commands
	void commandQuantizeNotes(int8_t offset, NudgeMode nudgeMode);
	void commandStartQuantize(int8_t offset, NudgeMode nudgeMode);
	ActionResult commandStopQuantize(int32_t y);
	void silenceAllAuditions();

	// auditionPadAction functions
	Drum* getAuditionedDrum(int32_t velocity, int32_t yDisplay, bool shiftButtonDown, Instrument* instrument,
	                        ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                        ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip);
	void potentiallyUpdateMultiRangeMenu(int32_t velocity, int32_t yDisplay, Instrument* instrument);
	void recordNoteOnEarly(int32_t velocity, int32_t yDisplay, Instrument* instrument, bool isKit,
	                       ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip, Drum* drum);
	void recordNoteOn(int32_t velocity, int32_t yDisplay, Instrument* instrument,
	                  ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
	                  ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip);
	void recordNoteOff(int32_t yDisplay, ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip);
	NoteRow* getNoteRowOnActiveClip(int32_t yDisplay, Instrument* instrument, bool clipIsActiveOnInstrument,
	                                ModelStackWithNoteRow* modelStackWithNoteRowOnCurrentClip, Drum* drum);
	int32_t getVelocityToSound(int32_t velocity);
	bool startAuditioningRow(int32_t velocity, int32_t yDisplay, bool shiftButtonDown, bool isKit,
	                         NoteRow* noteRowOnActiveClip, Drum* drum);
	void finishAuditioningRow(int32_t yDisplay, NoteRow* noteRowOnActiveClip);
};

extern InstrumentClipView instrumentClipView;
