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
#include "gui/colour/colour.h"
#include "gui/ui/keyboard/state_data.h"
#include "model/clip/clip.h"
#include "model/note/note_row_vector.h"
#include "model/timeline_counter.h"
#include "modulation/arpeggiator.h"
#include "util/d_string.h"

class Song;

class NoteRow;
class InstrumentClip;
class MidiCommand;
class Instrument;
class ModControllable;
class Drum;
class StereoSample;
class ParamManagerForTimeline;
class Sound;
class Note;
class ParamManagerMIDI;
class Action;
class ParamManagerForTimeline;
class ArrangementRow;
class TimelineView;
class ModelStackWithTimelineCounter;
class ModelStackWithModControllable;
class ModelStackWithNoteRow;

struct PendingNoteOn;

extern uint8_t undefinedColour[];

class InstrumentClip final : public Clip {
public:
	InstrumentClip(Song* song = NULL);
	~InstrumentClip();
	void increaseLengthWithRepeats(ModelStackWithTimelineCounter* modelStack, int32_t newLength,
	                               IndependentNoteRowLengthIncrease independentNoteRowInstruction,
	                               bool completelyRenderOutIterationDependence = false, Action* action = NULL);
	void halveNoteRowsWithIndependentLength(ModelStackWithTimelineCounter* modelStack);
	void repeatOrChopToExactLength(ModelStackWithTimelineCounter* modelStack, int32_t newLength);
	void processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t posIncrement);
	bool renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen, int32_t xScroll,
	                       uint32_t xZoom, RGB* image, uint8_t occupancyMask[], bool addUndefinedArea,
	                       int32_t noteRowIndexStart = 0, int32_t noteRowIndexEnd = 2147483647, int32_t xStart = 0,
	                       int32_t xEnd = kDisplayWidth, bool allowBlur = true, bool drawRepeats = false);
	void toggleNoteRowMute(ModelStackWithNoteRow* modelStack);
	void overviewMutePadPress(bool, bool);
	void midiCommandMute(bool);
	RGB getMainColourFromY(int32_t yNote, int8_t);
	void stopAllNotesPlaying(ModelStackWithTimelineCounter* modelStack, bool actuallySoundChange = true);
	void resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound = true);
	void setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos, bool useActualPosForParamManagers = true);
	void musicalModeChanged(uint8_t, int32_t, ModelStackWithTimelineCounter* modelStack);
	void seeWhatNotesWithinOctaveArePresent(bool[], int32_t, Song* song, bool deleteEmptyNoteRows = true);
	void transpose(int32_t, ModelStackWithTimelineCounter* modelStack);
	void expectNoFurtherTicks(Song* song, bool actuallySoundChange = true);
	int32_t clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing = false);
	NoteRow* createNewNoteRowForYVisual(int32_t, Song* song);
	void changeNoteOffsetAndCounteract(int32_t newNoteOffset);
	int32_t getYVisualFromYNote(int32_t, Song* song);
	int32_t getYNoteFromYVisual(int32_t, Song* song);
	int32_t getYNoteFromYDisplay(int32_t yDisplay, Song* song);
	int32_t guessRootNote(Song* song, int32_t previousRoot);
	void aboutToEdit();
	int32_t getNumNoteRows();
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithTimelineCounter* modelStack,
	                                                            Sound* sound);
	bool deleteSoundsWhichWontSound(Song* song);
	void setBackedUpParamManagerMIDI(ParamManagerForTimeline* newOne);
	void restoreBackedUpParamManagerMIDI(ModelStackWithModControllable* modelStack);
	int32_t getNoteRowId(NoteRow* noteRow, int32_t noteRowIndex);
	NoteRow* getNoteRowFromId(int32_t id);
	/// Return true if successfully shifted. Instrument clips always succeed
	bool shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int32_t amount);
	bool containsAnyNotes();
	ModelStackWithNoteRow* getNoteRowOnScreen(int32_t yDisplay, ModelStackWithTimelineCounter* modelStack);
	NoteRow* getNoteRowOnScreen(int32_t yDisplay, Song* song, int32_t* getIndex = NULL);
	bool currentlyScrollableAndZoomable();
	void recordNoteOn(ModelStackWithNoteRow* modelStack, int32_t velocity, bool forcePos0 = false,
	                  int16_t const* mpeValuesOrNull = NULL, int32_t fromMIDIChannel = MIDI_CHANNEL_NONE);
	void recordNoteOff(ModelStackWithNoteRow* modelStack, int32_t velocity = kDefaultLiftValue);

	void copyBasicsFrom(Clip* otherClip);

	ArpeggiatorSettings arpSettings; // Not valid for Kits
	int32_t arpeggiatorRate;
	int32_t arpeggiatorGate;

	ParamManagerForTimeline backedUpParamManagerMIDI;

	bool inScaleMode; // Probably don't quiz this directly - call isScaleModeClip() instead

	int32_t yScroll;

	// TODO: Unscope this once namespacing is done
	deluge::gui::ui::keyboard::KeyboardState keyboardState;

	int32_t ticksTilNextNoteRowEvent;
	int32_t noteRowsNumTicksBehindClip;

	LearnedMIDI
	    soundMidiCommand; // This is now handled by the Instrument, but for loading old songs, we need to capture and store this

	NoteRowVector noteRows;

	bool wrapEditing;
	uint32_t wrapEditLevel;

	// These *only* store a valid preset number for the instrument-types that the Clip is not currently on
	int8_t backedUpInstrumentSlot[4];
	int8_t backedUpInstrumentSubSlot[4];
	String backedUpInstrumentName[2];
	String backedUpInstrumentDirPath[2];

	bool affectEntire;

	bool onKeyboardScreen;

	//START ~ new Automation Clip View Variables
	bool onAutomationInstrumentClipView; //new to save the view that you are currently in
	                                     //(e.g. if you leave clip and want to come back where you left off)

	int32_t lastSelectedParamID;       //last selected Parameter to be edited in Automation Instrument Clip View
	Param::Kind lastSelectedParamKind; //0 = patched, 1 = unpatched, 2 = global effectable, 3 = none
	int32_t lastSelectedParamShortcutX;
	int32_t lastSelectedParamShortcutY;
	int32_t lastSelectedParamArrayPosition;
	InstrumentType lastSelectedInstrumentType;

	//END ~ new Automation Clip View Variables

	uint8_t midiBank; // 128 means none
	uint8_t midiSub;  // 128 means none
	uint8_t midiPGM;  // 128 means none

	InstrumentType instrumentTypeWhileLoading; // For use only while loading song

	void lengthChanged(ModelStackWithTimelineCounter* modelStack, int32_t oldLength, Action* action = NULL);
	NoteRow* createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, bool atStart, int32_t* getIndex = NULL);
	int32_t changeInstrument(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument,
	                         ParamManagerForTimeline* paramManager, InstrumentRemoval instrumentRemovalInstruction,
	                         InstrumentClip* favourClipForCloningParamManager = NULL,
	                         bool keepNoteRowsWithMIDIInput = true, bool giveMidiAssignmentsToNewInstrument = false);
	void detachFromOutput(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumName,
	                      bool shouldDeleteEmptyNoteRowsAtEndOfList = false, bool shouldRetainLinksToSounds = false,
	                      bool keepNoteRowsWithMIDIInput = true, bool shouldGrabMidiCommands = false,
	                      bool shouldBackUpExpressionParamsToo = true);
	void assignDrumsToNoteRows(ModelStackWithTimelineCounter* modelStack, bool shouldGiveMIDICommandsToDrums = false,
	                           int32_t numNoteRowsPreviouslyDeletedFromBottom = 0);
	void unassignAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumNames,
	                                  bool shouldRetainLinksToSounds, bool shouldGrabMidiCommands,
	                                  bool shouldBackUpExpressionParamsToo);
	int32_t readFromFile(Song* song);
	void writeDataToFile(Song* song);
	void prepNoteRowsForExitingKitMode(Song* song);
	void deleteNoteRow(ModelStackWithTimelineCounter* modelStack, int32_t i);
	int16_t getTopYNote();
	int16_t getBottomYNote();
	uint32_t getWrapEditLevel();
	ModelStackWithNoteRow* getNoteRowForYNote(int32_t yNote, ModelStackWithTimelineCounter* modelStack);
	NoteRow* getNoteRowForYNote(int32_t, int32_t* getIndex = NULL);
	ModelStackWithNoteRow* getNoteRowForSelectedDrum(ModelStackWithTimelineCounter* modelStack);
	ModelStackWithNoteRow* getNoteRowForDrum(ModelStackWithTimelineCounter* modelStack, Drum* drum);
	NoteRow* getNoteRowForDrum(Drum* drum, int32_t* getIndex = NULL);
	NoteRow* getOrCreateNoteRowForYNote(int32_t yNote, Song* song, Action* action = NULL, bool* scaleAltered = NULL,
	                                    int32_t* getNoteRowIndex = NULL);
	ModelStackWithNoteRow* getOrCreateNoteRowForYNote(int32_t yNote, ModelStackWithTimelineCounter* modelStack,
	                                                  Action* action = NULL, bool* scaleAltered = NULL);

	bool hasSameInstrument(InstrumentClip* otherClip);
	bool isScaleModeClip();
	bool allowNoteTails(ModelStackWithNoteRow* modelStack);
	int32_t setAudioInstrument(Instrument* newInstrument, Song* song, bool shouldNotifyInstrument,
	                           ParamManager* newParamManager, InstrumentClip* favourClipForCloningParamManager = NULL);

	void expectEvent();
	int32_t getDistanceToNextNote(Note* givenNote, ModelStackWithNoteRow* modelStack);
	void reGetParameterAutomation(ModelStackWithTimelineCounter* modelStack);
	void stopAllNotesForMIDIOrCV(ModelStackWithTimelineCounter* modelStack);
	void sendMIDIPGM();
	void noteRemovedFromMode(int32_t yNoteWithinOctave, Song* song);
	void clear(Action* action, ModelStackWithTimelineCounter* modelStack);
	bool doesProbabilityExist(int32_t apartFromPos, int32_t probability, int32_t secondProbability = -1);
	void clearArea(ModelStackWithTimelineCounter* modelStack, int32_t startPos, int32_t endPos, Action* action);
	ScaleType getScaleType();
	void backupPresetSlot();
	void setPosForParamManagers(ModelStackWithTimelineCounter* modelStack, bool useActualPos = true);
	ModelStackWithNoteRow* getNoteRowForDrumName(ModelStackWithTimelineCounter* modelStack, char const* name);
	void compensateVolumeForResonance(ModelStackWithTimelineCounter* modelStack);
	int32_t undoDetachmentFromOutput(ModelStackWithTimelineCounter* modelStack);
	int32_t setNonAudioInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager = NULL);
	int32_t setInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager,
	                      InstrumentClip* favourClipForCloningParamManager = NULL);
	void deleteOldDrumNames();
	void ensureScrollWithinKitBounds();
	bool isScrollWithinRange(int32_t scrollAmount, int32_t newYNote);
	int32_t appendClip(ModelStackWithTimelineCounter* thisModelStack, ModelStackWithTimelineCounter* otherModelStack);
	void instrumentBeenEdited();
	Instrument* changeInstrumentType(ModelStackWithTimelineCounter* modelStack, InstrumentType newInstrumentType);
	int32_t transferVoicesToOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
	                                                  ModelStackWithTimelineCounter* modelStackClone);
	void getSuggestedParamManager(Clip* newClip, ParamManagerForTimeline** suggestedParamManager, Sound* sound);
	int32_t claimOutput(ModelStackWithTimelineCounter* modelStack);
	char const* getXMLTag() { return "instrumentClip"; }
	void finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingLoop,
	                           int32_t buttonLatencyForTempolessRecord);
	int32_t beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency);
	Clip* cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStack, OverDubType newOverdubNature);
	bool isAbandonedOverdub();
	void quantizeLengthForArrangementRecording(ModelStackWithTimelineCounter* modelStack, int32_t lengthSoFar,
	                                           uint32_t timeRemainder, int32_t suggestedLength,
	                                           int32_t alternativeLongerLength);
	void setupAsNewKitClipIfNecessary(ModelStackWithTimelineCounter* modelStack);
	bool getCurrentlyRecordingLinearly();
	void abortRecording();
	void yDisplayNoLongerAuditioning(int32_t yDisplay, Song* song);
	void shiftOnlyOneNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int32_t shiftAmount);
	int32_t getMaxLength();
	bool hasAnyPitchExpressionAutomationOnNoteRows();
	ModelStackWithNoteRow* duplicateModelStackForClipBeingRecordedFrom(ModelStackWithNoteRow* modelStack,
	                                                                   char* otherModelStackMemory);
	void incrementPos(ModelStackWithTimelineCounter* modelStack, int32_t numTicks);

	// ----- TimelineCounter implementation -------
	void getActiveModControllable(ModelStackWithTimelineCounter* modelStack);

protected:
	void posReachedEnd(ModelStackWithTimelineCounter* modelStack);
	bool wantsToBeginLinearRecording(Song* song);
	bool cloneOutput(ModelStackWithTimelineCounter* modelStack);
	void pingpongOccurred(ModelStackWithTimelineCounter* modelStack);

private:
	InstrumentClip* instrumentWasLoadedByReferenceFromClip;

	void deleteEmptyNoteRowsAtEitherEnd(bool onlyIfNoDrum, ModelStackWithTimelineCounter* modelStack,
	                                    bool mustKeepLastOne = true, bool keepOnesWithMIDIInput = true);
	void sendPendingNoteOn(ModelStackWithTimelineCounter* modelStack, PendingNoteOn* pendingNoteOn);
	int32_t undoUnassignmentOfAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack);
	void deleteBackedUpParamManagerMIDI();
	bool possiblyDeleteEmptyNoteRow(NoteRow* noteRow, bool onlyIfNoDrum, Song* song, bool onlyIfNonNumeric = false,
	                                bool keepIfHasMIDIInput = true);
	void actuallyDeleteEmptyNoteRow(ModelStackWithNoteRow* modelStack);
	void prepareToEnterKitMode(Song* song);
	int32_t readMIDIParamsFromFile(int32_t readAutomationUpToPos);

	bool lastProbabilities[kNumProbabilityValues];
	int32_t lastProbabiltyPos[kNumProbabilityValues];
	bool currentlyRecordingLinearly;
};
