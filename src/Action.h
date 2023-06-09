/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#ifndef ACTION_H_
#define ACTION_H_

#include "r_typedefs.h"
#include "definitions.h"

class Consequence;
class InstrumentClip;
class ParamCollection;
class Sound;
class AutoParam;
class ConsequenceParamChange;
class Note;
class ActionClipState;
class UI;
class Instrument;
class ClipInstance;
class Song;
class Output;
class Clip;
class ClipArray;
class AudioClip;
class NoteVector;
class ModelStackWithAutoParam;
class ModelStack;

#define ACTION_MISC 0
#define ACTION_NOTE_EDIT 1
#define ACTION_NOTE_TAIL_EXTEND 2
#define ACTION_CLIP_LENGTH_INCREASE 3
#define ACTION_CLIP_LENGTH_DECREASE 4
#define ACTION_RECORD 5
#define ACTION_AUTOMATION_DELETE 6
#define ACTION_PARAM_UNAUTOMATED_VALUE_CHANGE 7
#define ACTION_SWING_CHANGE 8
#define ACTION_TEMPO_CHANGE 9
#define ACTION_CLIP_MULTIPLY 10
#define ACTION_CLIP_CLEAR 11
#define ACTION_CLIP_DELETE 12
#define ACTION_NOTES_PASTE 13
#define ACTION_AUTOMATION_PASTE 14
#define ACTION_CLIP_INSTANCE_EDIT 15
#define ACTION_ARRANGEMENT_TIME_EXPAND 16
#define ACTION_ARRANGEMENT_TIME_CONTRACT 17
#define ACTION_ARRANGEMENT_CLEAR 18
#define ACTION_ARRANGEMENT_RECORD 19
#define ACTION_INSTRUMENT_CLIP_HORIZONTAL_SHIFT 20
#define ACTION_NOTE_NUDGE 21
#define ACTION_NOTE_REPEAT_EDIT 22
#define ACTION_EUCLIDEAN_NUM_EVENTS_EDIT 23
#define ACTION_NOTEROW_ROTATE 24
#define ACTION_NOTEROW_LENGTH_EDIT 25
#define ACTION_NOTEROW_HORIZONTAL_SHIFT 26

class Action {
public:
	Action(int newActionType);
	void addConsequence(Consequence* consequence);
	int revert(int time, ModelStack* modelStack);
	bool containsConsequenceParamChange(ParamCollection* paramCollection, int paramId);
	void recordParamChangeIfNotAlreadySnapshotted(ModelStackWithAutoParam const* modelStack, bool stealData = false);
	void recordParamChangeDefinitely(ModelStackWithAutoParam const* modelStack, bool stealData);
	int recordNoteArrayChangeIfNotAlreadySnapshotted(InstrumentClip* clip, int noteRowId, NoteVector* noteVector,
	                                                 bool stealData, bool moveToFrontIfAlreadySnapshotted = false);
	int recordNoteArrayChangeDefinitely(InstrumentClip* clip, int noteRowId, NoteVector* noteVector, bool stealData);
	bool containsConsequenceNoteArrayChange(InstrumentClip* clip, int noteRowId, bool moveToFrontIfFound = false);
	void recordNoteExistenceChange(InstrumentClip* clip, int noteRowId, Note* note, int type);
	void recordNoteChange(InstrumentClip* clip, int noteRowId, Note* note, int32_t lengthAfter, int velocityAfter,
	                      int probabilityAfter);
	void updateYScrollClipViewAfter(InstrumentClip* clip = NULL);
	void recordClipInstanceExistenceChange(Output* output, ClipInstance* clipInstance, int type);
	void prepareForDestruction(int whichQueueActionIn, Song* song);
	void recordClipLengthChange(Clip* clip, int32_t oldLength);
	bool recordClipExistenceChange(Song* song, ClipArray* clipArray, Clip* clip, int type);
	void recordAudioClipSampleChange(AudioClip* clip);
	void deleteAllConsequences(int whichQueueActionIn, Song* song, bool destructing = false);

	uint8_t type;
	bool openForAdditions;

	// A bunch of snapshot-things here store their state both before or after the action - because the action could have changed these
	int xScrollClip[2];
	int yScrollSongView[2];
	int xZoomClip[2];

	int xScrollArranger[2];
	int yScrollArranger[2];
	int xZoomArranger[2];

	uint8_t modeNotes[2][12];
	uint8_t numModeNotes[2];

	// And a few more snapshot-things here only store one state - at the time of the action, because the action could not change these things
	uint8_t modKnobModeSongView;
	bool affectEntireSongView;

	bool tripletsOn;
	uint32_t tripletsLevel;

	//bool inKeyboardView;

	UI* view;

	Clip* currentClip; // Watch out - this might get set to NULL

	int32_t posToClearArrangementFrom;

	Action* nextAction;
	Consequence* firstConsequence;

	// We store these kinds of consequences separately because we need to be able to search through them fast, when there may be a large number of other kinds of consequences.
	// Also, these don't need re-ordering each time we revert
	ConsequenceParamChange* firstParamConsequence;

	ActionClipState* clipStates;

	uint32_t creationTime;

	int numClipStates;

	int8_t offset; // Recorded for the purpose of knowing when we can do those "partial undos"

private:
};

#endif /* ACTION_H_ */
