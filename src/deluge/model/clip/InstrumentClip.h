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

#ifndef INSTRUMENTCLIP_H
#define INSTRUMENTCLIP_H

#include <Clip.h>
#include <TimelineCounter.h>
#include "definitions.h"
#include "Arpeggiator.h"
#include "NoteRowVector.h"
#include "DString.h"

class Song;

class NoteRow;
class InstrumentClip;
class MidiCommand;
class Instrument;
class ModControllable;
class Drum;
class StereoSample;
class ParamManagerForTimeline;
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
	                               int independentNoteRowInstruction,
	                               bool completelyRenderOutIterationDependence = false, Action* action = NULL);
	void halveNoteRowsWithIndependentLength(ModelStackWithTimelineCounter* modelStack);
	void repeatOrChopToExactLength(ModelStackWithTimelineCounter* modelStack, int32_t newLength);
	void processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t posIncrement);
	bool renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen, int32_t xScroll,
	                       uint32_t xZoom, uint8_t* image, uint8_t occupancyMask[], bool addUndefinedArea,
	                       int noteRowIndexStart = 0, int noteRowIndexEnd = 2147483647, int xStart = 0,
	                       int xEnd = displayWidth, bool allowBlur = true, bool drawRepeats = false);
	void toggleNoteRowMute(ModelStackWithNoteRow* modelStack);
	void overviewMutePadPress(bool, bool);
	void midiCommandMute(bool);
	void getMainColourFromY(int yNote, int8_t, uint8_t[]);
	void stopAllNotesPlaying(ModelStackWithTimelineCounter* modelStack, bool actuallySoundChange = true);
	void resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound = true);
	void setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos, bool useActualPosForParamManagers = true);
	void musicalModeChanged(uint8_t, int, ModelStackWithTimelineCounter* modelStack);
	void seeWhatNotesWithinOctaveArePresent(bool[], int, Song* song, bool deleteEmptyNoteRows = true);
	void transpose(int, ModelStackWithTimelineCounter* modelStack);
	void expectNoFurtherTicks(Song* song, bool actuallySoundChange = true);
	int clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing = false);
	NoteRow* createNewNoteRowForYVisual(int, Song* song);
	void changeNoteOffsetAndCounteract(int newNoteOffset);
	int getYVisualFromYNote(int, Song* song);
	int getYNoteFromYVisual(int, Song* song);
	int getYNoteFromYDisplay(int yDisplay, Song* song);
	int guessRootNote(Song* song, int previousRoot);
	void aboutToEdit();
	int getNumNoteRows();
	void ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithTimelineCounter* modelStack,
	                                                            Sound* sound);
	bool deleteSoundsWhichWontSound(Song* song);
	void setBackedUpParamManagerMIDI(ParamManagerForTimeline* newOne);
	void restoreBackedUpParamManagerMIDI(ModelStackWithModControllable* modelStack);
	int getNoteRowId(NoteRow* noteRow, int noteRowIndex);
	NoteRow* getNoteRowFromId(int id);
	void shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int amount);
	bool containsAnyNotes();
	ModelStackWithNoteRow* getNoteRowOnScreen(int yDisplay, ModelStackWithTimelineCounter* modelStack);
	NoteRow* getNoteRowOnScreen(int yDisplay, Song* song, int* getIndex = NULL);
	bool currentlyScrollableAndZoomable();
	void recordNoteOn(ModelStackWithNoteRow* modelStack, int velocity, bool forcePos0 = false,
	                  int16_t const* mpeValuesOrNull = NULL, int fromMIDIChannel = MIDI_CHANNEL_NONE);
	void recordNoteOff(ModelStackWithNoteRow* modelStack, int velocity = DEFAULT_LIFT_VALUE);

	void copyBasicsFrom(Clip* otherClip);

	ArpeggiatorSettings arpSettings; // Not valid for Kits
	int32_t arpeggiatorRate;
	int32_t arpeggiatorGate;

	ParamManagerForTimeline backedUpParamManagerMIDI;

	bool inScaleMode; // Probably don't quiz this directly - call isScaleModeClip() instead

	int yScroll;
	int yScrollKeyboardScreen;

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

	uint8_t midiBank; // 128 means none
	uint8_t midiSub;  // 128 means none
	uint8_t midiPGM;  // 128 means none

	uint8_t instrumentTypeWhileLoading; // For use only while loading song

	void lengthChanged(ModelStackWithTimelineCounter* modelStack, int32_t oldLength, Action* action = NULL);
	NoteRow* createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, bool atStart, int* getIndex = NULL);
	int changeInstrument(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument,
	                     ParamManagerForTimeline* paramManager, int instrumentRemovalInstruction,
	                     InstrumentClip* favourClipForCloningParamManager = NULL, bool keepNoteRowsWithMIDIInput = true,
	                     bool giveMidiAssignmentsToNewInstrument = false);
	void detachFromOutput(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumName,
	                      bool shouldDeleteEmptyNoteRowsAtEndOfList = false, bool shouldRetainLinksToSounds = false,
	                      bool keepNoteRowsWithMIDIInput = true, bool shouldGrabMidiCommands = false,
	                      bool shouldBackUpExpressionParamsToo = true);
	void assignDrumsToNoteRows(ModelStackWithTimelineCounter* modelStack, bool shouldGiveMIDICommandsToDrums = false,
	                           int numNoteRowsPreviouslyDeletedFromBottom = 0);
	void unassignAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumNames,
	                                  bool shouldRetainLinksToSounds, bool shouldGrabMidiCommands,
	                                  bool shouldBackUpExpressionParamsToo);
	int readFromFile(Song* song);
	void writeDataToFile(Song* song);
	void prepNoteRowsForExitingKitMode(Song* song);
	void deleteNoteRow(ModelStackWithTimelineCounter* modelStack, int i);
	int16_t getTopYNote();
	int16_t getBottomYNote();
	uint32_t getWrapEditLevel();
	ModelStackWithNoteRow* getNoteRowForYNote(int yNote, ModelStackWithTimelineCounter* modelStack);
	NoteRow* getNoteRowForYNote(int, int* getIndex = NULL);
	ModelStackWithNoteRow* getNoteRowForSelectedDrum(ModelStackWithTimelineCounter* modelStack);
	ModelStackWithNoteRow* getNoteRowForDrum(ModelStackWithTimelineCounter* modelStack, Drum* drum);
	NoteRow* getNoteRowForDrum(Drum* drum, int* getIndex = NULL);
	NoteRow* getOrCreateNoteRowForYNote(int yNote, Song* song, Action* action = NULL, bool* scaleAltered = NULL,
	                                    int* getNoteRowIndex = NULL);
	ModelStackWithNoteRow* getOrCreateNoteRowForYNote(int yNote, ModelStackWithTimelineCounter* modelStack,
	                                                  Action* action = NULL, bool* scaleAltered = NULL);

	bool hasSameInstrument(InstrumentClip* otherClip);
	bool isScaleModeClip();
	bool allowNoteTails(ModelStackWithNoteRow* modelStack);
	int setAudioInstrument(Instrument* newInstrument, Song* song, bool shouldNotifyInstrument,
	                       ParamManager* newParamManager, InstrumentClip* favourClipForCloningParamManager = NULL);

	void expectEvent();
	int32_t getDistanceToNextNote(Note* givenNote, ModelStackWithNoteRow* modelStack);
	void reGetParameterAutomation(ModelStackWithTimelineCounter* modelStack);
	void stopAllNotesForMIDIOrCV(ModelStackWithTimelineCounter* modelStack);
	void sendMIDIPGM();
	void noteRemovedFromMode(int yNoteWithinOctave, Song* song);
	void clear(Action* action, ModelStackWithTimelineCounter* modelStack);
	bool doesProbabilityExist(int32_t apartFromPos, int probability, int secondProbability = -1);
	void clearArea(ModelStackWithTimelineCounter* modelStack, int32_t startPos, int32_t endPos, Action* action);
	int getScaleType();
	void backupPresetSlot();
	void setPosForParamManagers(ModelStackWithTimelineCounter* modelStack, bool useActualPos = true);
	ModelStackWithNoteRow* getNoteRowForDrumName(ModelStackWithTimelineCounter* modelStack, char const* name);
	void compensateVolumeForResonance(ModelStackWithTimelineCounter* modelStack);
	int undoDetachmentFromOutput(ModelStackWithTimelineCounter* modelStack);
	int setNonAudioInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager = NULL);
	int setInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager,
	                  InstrumentClip* favourClipForCloningParamManager = NULL);
	void deleteOldDrumNames();
	void ensureScrollWithinKitBounds();
	bool isScrollWithinRange(int scrollAmount, int newYNote);
	int appendClip(ModelStackWithTimelineCounter* thisModelStack, ModelStackWithTimelineCounter* otherModelStack);
	void instrumentBeenEdited();
	Instrument* changeInstrumentType(ModelStackWithTimelineCounter* modelStack, int newInstrumentType);
	int transferVoicesToOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
	                                              ModelStackWithTimelineCounter* modelStackClone);
	void getSuggestedParamManager(Clip* newClip, ParamManagerForTimeline** suggestedParamManager, Sound* sound);
	int claimOutput(ModelStackWithTimelineCounter* modelStack);
	char const* getXMLTag() { return "instrumentClip"; }
	void finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingLoop,
	                           int buttonLatencyForTempolessRecord);
	int beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int buttonPressLatency);
	Clip* cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStack, int newOverdubNature);
	bool isAbandonedOverdub();
	void quantizeLengthForArrangementRecording(ModelStackWithTimelineCounter* modelStack, int32_t lengthSoFar,
	                                           uint32_t timeRemainder, int32_t suggestedLength,
	                                           int32_t alternativeLongerLength);
	void setupAsNewKitClipIfNecessary(ModelStackWithTimelineCounter* modelStack);
	bool getCurrentlyRecordingLinearly();
	void abortRecording();
	void yDisplayNoLongerAuditioning(int yDisplay, Song* song);
	void shiftOnlyOneNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int shiftAmount);
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
	int undoUnassignmentOfAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack);
	void deleteBackedUpParamManagerMIDI();
	bool possiblyDeleteEmptyNoteRow(NoteRow* noteRow, bool onlyIfNoDrum, Song* song, bool onlyIfNonNumeric = false,
	                                bool keepIfHasMIDIInput = true);
	void actuallyDeleteEmptyNoteRow(ModelStackWithNoteRow* modelStack);
	void prepareToEnterKitMode(Song* song);
	int readMIDIParamsFromFile(int32_t readAutomationUpToPos);

	bool lastProbabilities[NUM_PROBABILITY_VALUES];
	int32_t lastProbabiltyPos[NUM_PROBABILITY_VALUES];
	bool currentlyRecordingLinearly;
};

#endif
