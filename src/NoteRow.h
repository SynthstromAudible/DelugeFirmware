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

#ifndef NoteRow_h
#define NoteRow_h

#include <ParamManager.h>
#include "definitions.h"
#include "NoteVector.h"
#include "LearnedMIDI.h"

#define SQUARE_NEW_NOTE 1
#define SQUARE_NOTE_HEAD 2
#define SQUARE_NOTE_TAIL_UNMODIFIED 3
#define SQUARE_NOTE_TAIL_MODIFIED 4
#define SQUARE_BLURRED 5

#define CORRESPONDING_NOTES_ADJUST_VELOCITY 0
#define CORRESPONDING_NOTES_SET_PROBABILITY 1

class InstrumentClip;
class Song;
class PlaybackHandler;
class Note;
class Drum;
class ParamManagerForTimeline;
class Kit;
class DrumName;
class Action;
class NoteRow;
class CopiedNoteRow;
class Sound;
class TimelineView;
class ModelStackWithTimelineCounter;
class ModelStackWithNoteRow;
class ParamManagerForTimeline;

struct PendingNoteOn {
	NoteRow* noteRow;
	int noteRowId;
	uint32_t sampleSyncLength;
	int32_t ticksLate;
	uint8_t probability;
	uint8_t velocity;
};

struct PendingNoteOnList {
	PendingNoteOn pendingNoteOns[maxNumNoteOnsPending];
	uint8_t count;
};

#define STATUS_OFF 0
#define STATUS_SEQUENCED_NOTE 1

class NoteRow {
public:
	NoteRow(int16_t newY = -32768);
	~NoteRow();
	void renderRow(TimelineView* editorScreen, uint8_t[], uint8_t[], uint8_t[], uint8_t* image, uint8_t[], bool,
	               uint32_t, bool allowNoteTails, int imageWidth, int32_t xScroll, uint32_t xZoom, int xStart = 0,
	               int xEnd = displayWidth, bool drawRepeats = false);
	void deleteNoteByPos(ModelStackWithNoteRow* modelStack, int32_t pos, Action* action);
	void stopCurrentlyPlayingNote(ModelStackWithNoteRow* modelStack, bool actuallySoundChange = true,
	                              Note* note = NULL);
	bool generateRepeats(ModelStackWithNoteRow* modelStack, uint32_t oldLength, uint32_t newLength,
	                     int numRepeatsRounded, Action* action);
	void toggleMute(ModelStackWithNoteRow* modelStack, bool clipIsActiveAndPlaybackIsOn);
	bool hasNoNotes();
	void resumePlayback(ModelStackWithNoteRow* modelStack, bool clipMayMakeSound);
	void writeToFile(int drumIndex, InstrumentClip* clip);
	int readFromFile(int*, InstrumentClip*, Song* song, int32_t readAutomationUpToPos);
	inline int getNoteCode() { return y; }
	void writeToFlash();
	void readFromFlash(InstrumentClip* parentClip);
	unsigned int getNumNotes();
	void setDrum(Drum* newDrum, Kit* kit, ModelStackWithNoteRow* modelStack,
	             InstrumentClip* favourClipForCloningParamManager = NULL, ParamManager* paramManager = NULL);

	int32_t getDistanceToNextNote(int32_t pos, ModelStackWithNoteRow const* modelStack, bool reversed = false);

	int16_t y; // This has to be at the top
	bool muted;

	int32_t loopLengthIfIndependent; // 0 means obeying parent
	int32_t lastProcessedPosIfIndependent;
	int32_t repeatCountIfIndependent;
	bool
	    currentlyPlayingReversedIfIndependent; // Valid only if not obeying parent, or if obeyed parent is pingponging and we have independent length
	uint8_t sequenceDirectionMode;
	uint32_t getLivePos(ModelStackWithNoteRow const* modelStack);
	bool hasIndependentPlayPos();

	ParamManagerForTimeline paramManager;
	Drum* drum;
	DrumName* firstOldDrumName;
	NoteVector notes;

	// These are deprecated, and only used during loading for compatibility with old song files
	LearnedMIDI muteMIDICommand;
	LearnedMIDI midiInput;

	int8_t colourOffset;

	uint8_t
	    soundingStatus; // External classes aren't really supposed to set this to OFF. Call something like cancelAutitioning() instead - which calls Clip::expectEvent(), which is needed

	bool
	    skipNextNote; // To be used if we recorded a note which was quantized forwards, and we have to remember not to play it

	int attemptNoteAdd(int32_t pos, int32_t length, int velocity, int probability, ModelStackWithNoteRow* modelStack,
	                   Action* action);
	int attemptNoteAddReversed(ModelStackWithNoteRow* modelStack, int32_t pos, int velocity, bool allowingNoteTails);
	int addCorrespondingNotes(int32_t pos, int32_t length, uint8_t velocity, ModelStackWithNoteRow* modelStack,
	                          bool allowNoteTails, Action* action);
	int32_t processCurrentPos(ModelStackWithNoteRow* modelStack, int32_t ticksSinceLast,
	                          PendingNoteOnList* pendingNoteOnList);
	uint8_t getSquareType(int32_t squareStart, int32_t squareWidth, Note** firstNote, Note** lastNote,
	                      ModelStackWithNoteRow* modelStack, bool allowNoteTails, int32_t desiredNoteLength,
	                      Action* action, bool clipCurrentlyPlaying, bool extendPreviousNoteIfPossible);
	int clearArea(int32_t areaStart, int32_t areaWidth, ModelStackWithNoteRow* modelStack, Action* action,
	              uint32_t wrapEditLevel, bool actuallyExtendNoteAtStartOfArea = false);
	void trimToLength(uint32_t newLength, ModelStackWithNoteRow* modelStack, Action* action);
	void trimNoteDataToNewClipLength(uint32_t newLength, InstrumentClip* clip, Action* action, int noteRowId);
	void recordNoteOff(uint32_t pos, ModelStackWithNoteRow* modelStack, Action* action, int velocity);
	int8_t getColourOffset(InstrumentClip* clip);
	void rememberDrumName();
	void shiftHorizontally(int amount, ModelStackWithNoteRow* modelStack);
	void clear(Action* action, ModelStackWithNoteRow* modelStack);
	bool doesProbabilityExist(int32_t apartFromPos, int probability, int secondProbability = -1);
	bool paste(ModelStackWithNoteRow* modelStack, CopiedNoteRow* copiedNoteRow, float scaleFactor, int32_t screenEndPos,
	           Action* action);
	void giveMidiCommandsToDrum();
	void grabMidiCommandsFromDrum();
	void deleteParamManager(bool shouldUpdatePointer = true);
	void deleteOldDrumNames(bool shouldUpdatePointer = true);
	int appendNoteRow(ModelStackWithNoteRow* thisModelStack, ModelStackWithNoteRow* otherModelStack, int offset,
	                  int whichRepeatThisIs, int32_t otherClipLength);
	int beenCloned(ModelStackWithNoteRow* modelStack, bool shouldFlattenReversing);
	void resumeOriginalNoteRowFromThisClone(ModelStackWithNoteRow* modelStackOriginal,
	                                        ModelStackWithNoteRow* modelStackClone);
	void silentlyResumePlayback(ModelStackWithNoteRow* modelStack);
	void trimParamManager(ModelStackWithNoteRow* modelStack);
	void deleteNoteByIndex(int index, Action* action, int noteRowId, InstrumentClip* clip);
	void complexSetNoteLength(Note* thisNote, uint32_t newLength, ModelStackWithNoteRow* modelStack, Action* action);
	int changeNotesAcrossAllScreens(int32_t editPos, ModelStackWithNoteRow* modelStack, Action* action, int changeType,
	                                int changeValue);
	int nudgeNotesAcrossAllScreens(int32_t editPos, ModelStackWithNoteRow* modelStack, Action* action,
	                               uint32_t wrapEditLevel, int nudgeOffset);
	int editNoteRepeatAcrossAllScreens(int32_t editPos, int32_t squareWidth, ModelStackWithNoteRow* modelStack,
	                                   Action* action, uint32_t wrapEditLevel, int newNumNotes);
	void setLength(ModelStackWithNoteRow* modelStack, int32_t newLength, Action* actionToRecordTo, int32_t oldPos,
	               bool hadIndependentPlayPosBefore);
	void getMPEValues(ModelStackWithNoteRow* modelStack, int16_t* mpeValues);
	void clearMPEUpUntilNextNote(ModelStackWithNoteRow* modelStack, int32_t pos, int32_t wrapEditLevel,
	                             bool shouldJustDeleteNodes = false);
	int getEffectiveSequenceDirectionMode(ModelStackWithNoteRow const* modelStack);
	bool recordPolyphonicExpressionEvent(ModelStackWithNoteRow* modelStackWithNoteRow, int32_t newValueBig,
	                                     int whichExpressionDimension, bool forDrum);
	void setSequenceDirectionMode(ModelStackWithNoteRow* modelStack, int newMode);
	bool isAuditioning(ModelStackWithNoteRow* modelStack);

private:
	void noteOn();
	void noteOff();
	void playNote(bool, ModelStackWithNoteRow* modelStack, Note*, int32_t ticksLate = 0, uint32_t samplesLate = 0,
	              bool noteMightBeConstant = false, PendingNoteOnList* pendingNoteOnList = NULL);
	void playNextNote(InstrumentClip*, bool, bool noteMightBeConstant = false,
	                  PendingNoteOnList* pendingNoteOnList = NULL);
	void findNextNoteToPlay(unsigned int);
	void attemptLateStartOfNextNoteToPlay(ModelStackWithNoteRow* modelStack, Note* note);
	bool noteRowMayMakeSound(bool);
	void drawTail(int32_t startTail, int32_t endTail, uint8_t squareColour[], bool overwriteExisting,
	              uint8_t image[][3], uint8_t occupancyMask[]);
};

#endif
