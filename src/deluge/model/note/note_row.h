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
#include "io/midi/learned_midi.h"
#include "model/iterance/iterance.h"
#include "model/note/note_vector.h"
#include "modulation/params/param_manager.h"

#define SQUARE_NEW_NOTE 1
#define SQUARE_NOTE_HEAD 2
#define SQUARE_NOTE_TAIL_UNMODIFIED 3
#define SQUARE_NOTE_TAIL_MODIFIED 4
#define SQUARE_BLURRED 5
#define SQUARE_NO_NOTE 6
#define SQUARE_NOTE_TAIL 7

#define CORRESPONDING_NOTES_ADJUST_VELOCITY 0
#define CORRESPONDING_NOTES_SET_PROBABILITY 1
#define CORRESPONDING_NOTES_SET_VELOCITY 2
#define CORRESPONDING_NOTES_SET_ITERANCE 3
#define CORRESPONDING_NOTES_SET_FILL 4

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

struct SquareInfo {
	Note* firstNote;
	int32_t squareStartPos;
	int32_t squareEndPos;
	int32_t numNotes;
	uint8_t squareType;
	int32_t averageVelocity;
	int32_t probability;
	Iterance iterance;
	int32_t fill;
	bool isValid{false};
};

struct PendingNoteOn {
	NoteRow* noteRow;
	int32_t noteRowId;
	uint32_t sampleSyncLength;
	int32_t ticksLate;
	uint8_t probability;
	uint8_t velocity;
	Iterance iterance;
	uint8_t fill;
};

struct PendingNoteOnList {
	PendingNoteOn pendingNoteOns[kMaxNumNoteOnsPending];
	uint8_t count;
};

constexpr int32_t kQuantizationPrecision = 10;

#define STATUS_OFF 0
#define STATUS_SEQUENCED_NOTE 1

/// An ordered list of notes which all share the same nominal y value.
///
/// In kits, the y value represents the row within the kit directly. In other types of clips, the y value maps to a MIDI
/// pitch value.
///
/// Notes within the row must not overlap -- the end location of each note (described by note.pos + note.length) must be
/// strictly less than the start location of the next note. The length of the last note in the row can exceed the loop
/// length of this NoteRow (either loopLengthIfIndependent if that value is nonzero, or the loop length of the clip
/// containing this NoteRow).
class NoteRow {
public:
	NoteRow(int16_t newY = -32768);
	~NoteRow();
	void renderRow(TimelineView* editorScreen, RGB, RGB, RGB, RGB* image, uint8_t[], bool, uint32_t,
	               bool allowNoteTails, int32_t imageWidth, int32_t xScroll, uint32_t xZoom, int32_t xStart = 0,
	               int32_t xEnd = kDisplayWidth, bool drawRepeats = false);
	void deleteNoteByPos(ModelStackWithNoteRow* modelStack, int32_t pos, Action* action);
	void stopCurrentlyPlayingNote(ModelStackWithNoteRow* modelStack, bool actuallySoundChange = true,
	                              Note* note = NULL);
	bool generateRepeats(ModelStackWithNoteRow* modelStack, uint32_t oldLength, uint32_t newLength,
	                     int32_t numRepeatsRounded, Action* action);
	void toggleMute(ModelStackWithNoteRow* modelStack, bool clipIsActiveAndPlaybackIsOn);
	bool hasNoNotes();
	void resumePlayback(ModelStackWithNoteRow* modelStack, bool clipMayMakeSound);
	void writeToFile(Serializer& writer, int32_t drumIndex, InstrumentClip* clip);
	Error readFromFile(Deserializer& reader, int32_t*, InstrumentClip*, Song* song, int32_t readAutomationUpToPos);
	inline int32_t getNoteCode() { return y; }
	void writeToFlash();
	void readFromFlash(InstrumentClip* parentClip);
	uint32_t getNumNotes();
	void setDrum(Drum* newDrum, Kit* kit, ModelStackWithNoteRow* modelStack,
	             InstrumentClip* favourClipForCloningParamManager = NULL, ParamManager* paramManager = NULL,
	             bool backupOldParamManager = true);

	int32_t getDistanceToNextNote(int32_t pos, ModelStackWithNoteRow const* modelStack, bool reversed = false);

	int16_t y; // This has to be at the top
	bool muted;

	int32_t loopLengthIfIndependent; // 0 means obeying parent
	int32_t lastProcessedPosIfIndependent;
	int32_t repeatCountIfIndependent;

	// Valid only if not obeying parent, or if obeyed parent is pingponging and we have independent length
	bool currentlyPlayingReversedIfIndependent;

	SequenceDirection sequenceDirectionMode;
	uint32_t getLivePos(ModelStackWithNoteRow const* modelStack);
	bool hasIndependentPlayPos();

	ParamManagerForTimeline paramManager;
	Drum* drum;
	DrumName* firstOldDrumName;
	NoteVector notes;
	// values for whole row
	uint8_t probabilityValue;
	Iterance iteranceValue;
	uint8_t fillValue;
	// These are deprecated, and only used during loading for compatibility with old song files
	LearnedMIDI muteMIDICommand;
	LearnedMIDI midiInput;

	int8_t colourOffset;

	// External classes aren't really supposed to set this to OFF. Call something like cancelAutitioning() instead -
	// which calls Clip::expectEvent(), which is needed
	uint8_t soundingStatus;

	/// Time before which all note events should be ignored during live playback. 0 means all notes should play (i.e. a
	/// note event at the time stored here should be allowed to sound). When doing quantized recording, we might have
	/// quantized the note to a later point in time so this is used to inhibit re-sounding of the quantized note.
	///
	/// This is always stored in "forward time", so even when playback is reversed this can only be meaningfully be
	/// compared with the time since this NoteRow started (i.e., time from the end during reversed playback).
	uint32_t ignoreNoteOnsBefore_;

	int32_t getDefaultProbability();
	Iterance getDefaultIterance();
	int32_t getDefaultFill(ModelStackWithNoteRow* modelStack);
	int32_t attemptNoteAdd(int32_t pos, int32_t length, int32_t velocity, int32_t probability, Iterance iterance,
	                       int32_t fill, ModelStackWithNoteRow* modelStack, Action* action);
	int32_t attemptNoteAddReversed(ModelStackWithNoteRow* modelStack, int32_t pos, int32_t velocity,
	                               bool allowingNoteTails);
	Error addCorrespondingNotes(int32_t pos, int32_t length, uint8_t velocity, ModelStackWithNoteRow* modelStack,
	                            bool allowNoteTails, Action* action);
	int32_t processCurrentPos(ModelStackWithNoteRow* modelStack, int32_t ticksSinceLast,
	                          PendingNoteOnList* pendingNoteOnList);
	void initRowSquareInfo(SquareInfo rowSquareInfo[kDisplayWidth], bool anyNotes);
	void initSquareInfo(SquareInfo& squareInfo, bool anyNotes, int32_t x);
	void getRowSquareInfo(int32_t effectiveLength, SquareInfo rowSquareInfo[kDisplayWidth]);
	void getSquareInfo(int32_t x, int32_t effectiveLength, SquareInfo& squareInfo);
	void addNotesToSquareInfo(int32_t effectiveLength, SquareInfo& squareInfo, int32_t& noteIndex, Note** note);
	void calculateSquareAverages(SquareInfo& squareInfo);
	uint8_t getSquareType(int32_t squareStart, int32_t squareWidth, Note** firstNote, Note** lastNote,
	                      ModelStackWithNoteRow* modelStack, bool allowNoteTails, int32_t desiredNoteLength,
	                      Action* action, bool clipCurrentlyPlaying, bool extendPreviousNoteIfPossible);
	Error clearArea(int32_t areaStart, int32_t areaWidth, ModelStackWithNoteRow* modelStack, Action* action,
	                uint32_t wrapEditLevel, bool actuallyExtendNoteAtStartOfArea = false);
	void trimToLength(uint32_t newLength, ModelStackWithNoteRow* modelStack, Action* action);
	void trimNoteDataToNewClipLength(uint32_t newLength, InstrumentClip* clip, Action* action, int32_t noteRowId);
	void recordNoteOff(uint32_t pos, ModelStackWithNoteRow* modelStack, Action* action, int32_t velocity);
	int8_t getColourOffset(InstrumentClip* clip);
	void rememberDrumName();
	void shiftHorizontally(int32_t amount, ModelStackWithNoteRow* modelStack, bool shiftAutomation,
	                       bool shiftSequenceAndMPE);
	void clear(Action* action, ModelStackWithNoteRow* modelStack, bool clearAutomation, bool clearSequenceAndMPE);
	bool doesProbabilityExist(int32_t apartFromPos, int32_t probability, int32_t secondProbability = -1);
	bool paste(ModelStackWithNoteRow* modelStack, CopiedNoteRow* copiedNoteRow, float scaleFactor, int32_t screenEndPos,
	           Action* action);
	void giveMidiCommandsToDrum();
	void grabMidiCommandsFromDrum();
	void deleteParamManager(bool shouldUpdatePointer = true);
	void deleteOldDrumNames(bool shouldUpdatePointer = true);
	Error appendNoteRow(ModelStackWithNoteRow* thisModelStack, ModelStackWithNoteRow* otherModelStack, int32_t offset,
	                    int32_t whichRepeatThisIs, int32_t otherClipLength);
	Error beenCloned(ModelStackWithNoteRow* modelStack, bool shouldFlattenReversing);
	void resumeOriginalNoteRowFromThisClone(ModelStackWithNoteRow* modelStackOriginal,
	                                        ModelStackWithNoteRow* modelStackClone);
	void silentlyResumePlayback(ModelStackWithNoteRow* modelStack);
	void trimParamManager(ModelStackWithNoteRow* modelStack);
	void deleteNoteByIndex(int32_t index, Action* action, int32_t noteRowId, InstrumentClip* clip);
	void complexSetNoteLength(Note* thisNote, uint32_t newLength, ModelStackWithNoteRow* modelStack, Action* action);
	Error changeNotesAcrossAllScreens(int32_t editPos, ModelStackWithNoteRow* modelStack, Action* action,
	                                  int32_t changeType, int32_t changeValue);
	/// Nudge the note at editPos by either +1 (if nudgeOffset > 0) or -1 (if nudgeOffset < 0)
	///
	/// The caller must call Clip::expectEvent on the clip containing this `NoteRow` after this.
	Error nudgeNotesAcrossAllScreens(int32_t editPos, ModelStackWithNoteRow* modelStack, Action* action,
	                                 uint32_t wrapEditLevel, int32_t nudgeOffset);
	/// Quantize the notes in this NoteRow so their positions are within `(kQuantizationPrecision - amount) *
	/// increment/kQuantizationPrecision` of the grid defined by `n * increment`. If `amount` is negative, the row is
	/// instead "humanized" by jittering the note positions to `±amount * increment / kQuantizationPrecision` sequencer
	/// ticks of the `n * increment` grid.
	///
	/// The caller must call Clip::expectEvent on the clip containing this `NoteRow` after this.
	Error quantize(ModelStackWithNoteRow* modelStack, int32_t increment, int32_t amount);
	Error editNoteRepeatAcrossAllScreens(int32_t editPos, int32_t squareWidth, ModelStackWithNoteRow* modelStack,
	                                     Action* action, uint32_t wrapEditLevel, int32_t newNumNotes);
	void setLength(ModelStackWithNoteRow* modelStack, int32_t newLength, Action* actionToRecordTo, int32_t oldPos,
	               bool hadIndependentPlayPosBefore);
	void getMPEValues(ModelStackWithNoteRow* modelStack, int16_t* mpeValues);
	void clearMPEUpUntilNextNote(ModelStackWithNoteRow* modelStack, int32_t pos, int32_t wrapEditLevel,
	                             bool shouldJustDeleteNodes = false);
	SequenceDirection getEffectiveSequenceDirectionMode(ModelStackWithNoteRow const* modelStack);
	bool recordPolyphonicExpressionEvent(ModelStackWithNoteRow* modelStackWithNoteRow, int32_t newValueBig,
	                                     int32_t whichExpressionDimension, bool forDrum);
	void setSequenceDirectionMode(ModelStackWithNoteRow* modelStack, SequenceDirection newMode);
	bool isAuditioning(ModelStackWithNoteRow* modelStack);

private:
	void playNote(bool, ModelStackWithNoteRow* modelStack, Note*, int32_t ticksLate = 0, uint32_t samplesLate = 0,
	              bool noteMightBeConstant = false, PendingNoteOnList* pendingNoteOnList = NULL);
	void playNextNote(InstrumentClip*, bool, bool noteMightBeConstant = false,
	                  PendingNoteOnList* pendingNoteOnList = NULL);
	void findNextNoteToPlay(uint32_t);
	void attemptLateStartOfNextNoteToPlay(ModelStackWithNoteRow* modelStack, Note* note);
	bool noteRowMayMakeSound(bool);
	void drawTail(int32_t startTail, int32_t endTail, uint8_t squareColour[], bool overwriteExisting,
	              uint8_t image[][3], uint8_t occupancyMask[]);
};
