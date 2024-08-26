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

#pragma once

#include "definitions_cxx.hpp"
#include "model/scale/note_set.h"
#include <cstdint>

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

enum class ActionType {
	MISC,
	NOTE_EDIT,
	NOTE_TAIL_EXTEND,
	CLIP_LENGTH_INCREASE,
	CLIP_LENGTH_DECREASE,
	RECORD,
	AUTOMATION_DELETE,
	PARAM_UNAUTOMATED_VALUE_CHANGE,
	SWING_CHANGE,
	TEMPO_CHANGE,
	CLIP_MULTIPLY,
	CLIP_CLEAR,
	CLIP_DELETE,
	NOTES_PASTE,
	PATTERN_PASTE,
	AUTOMATION_PASTE,
	CLIP_INSTANCE_EDIT,
	ARRANGEMENT_TIME_EXPAND,
	ARRANGEMENT_TIME_CONTRACT,
	ARRANGEMENT_CLEAR,
	ARRANGEMENT_RECORD,
	CLIP_HORIZONTAL_SHIFT,
	NOTE_NUDGE,
	NOTE_REPEAT_EDIT,
	EUCLIDEAN_NUM_EVENTS_EDIT,
	NOTEROW_ROTATE,
	NOTEROW_LENGTH_EDIT,
	NOTEROW_HORIZONTAL_SHIFT,
};

class Action {
public:
	Action(ActionType newActionType);
	void addConsequence(Consequence* consequence);
	Error revert(TimeType time, ModelStack* modelStack);
	bool containsConsequenceParamChange(ParamCollection* paramCollection, int32_t paramId);
	void recordParamChangeIfNotAlreadySnapshotted(ModelStackWithAutoParam const* modelStack, bool stealData = false);
	void recordParamChangeDefinitely(ModelStackWithAutoParam const* modelStack, bool stealData);
	Error recordNoteArrayChangeIfNotAlreadySnapshotted(InstrumentClip* clip, int32_t noteRowId, NoteVector* noteVector,
	                                                   bool stealData, bool moveToFrontIfAlreadySnapshotted = false);
	Error recordNoteArrayChangeDefinitely(InstrumentClip* clip, int32_t noteRowId, NoteVector* noteVector,
	                                      bool stealData);
	bool containsConsequenceNoteArrayChange(InstrumentClip* clip, int32_t noteRowId, bool moveToFrontIfFound = false);
	void recordNoteExistenceChange(InstrumentClip* clip, int32_t noteRowId, Note* note, ExistenceChangeType type);
	void recordNoteChange(InstrumentClip* clip, int32_t noteRowId, Note* note, int32_t lengthAfter,
	                      int32_t velocityAfter, int32_t probabilityAfter);
	void updateYScrollClipViewAfter(InstrumentClip* clip = NULL);
	void recordClipInstanceExistenceChange(Output* output, ClipInstance* clipInstance, ExistenceChangeType type);
	void prepareForDestruction(int32_t whichQueueActionIn, Song* song);
	void recordClipLengthChange(Clip* clip, int32_t oldLength);
	bool recordClipExistenceChange(Song* song, ClipArray* clipArray, Clip* clip, ExistenceChangeType type);
	void recordAudioClipSampleChange(AudioClip* clip);
	void deleteAllConsequences(int32_t whichQueueActionIn, Song* song, bool destructing = false);

	ActionType type;
	bool openForAdditions;

	// A bunch of snapshot-things here store their state both before or after the action - because the action could have
	// changed these
	int32_t xScrollClip[2];
	int32_t yScrollSongView[2];
	int32_t xZoomClip[2];

	int32_t xScrollArranger[2];
	int32_t yScrollArranger[2];
	int32_t xZoomArranger[2];

	NoteSet modeNotes[2];

	// And a few more snapshot-things here only store one state - at the time of the action, because the action could
	// not change these things
	uint8_t modKnobModeSongView;
	bool affectEntireSongView;

	bool tripletsOn;
	uint32_t tripletsLevel;

	// bool inKeyboardView;

	UI* view;

	Clip* currentClip; // Watch out - this might get set to NULL

	int32_t posToClearArrangementFrom;

	Action* nextAction;
	Consequence* firstConsequence;

	// We store these kinds of consequences separately because we need to be able to search through them fast, when
	// there may be a large number of other kinds of consequences. Also, these don't need re-ordering each time we
	// revert
	ConsequenceParamChange* firstParamConsequence;

	ActionClipState* clipStates;

	uint32_t creationTime;

	int32_t numClipStates;

	int8_t offset; // Recorded for the purpose of knowing when we can do those "partial undos"

private:
};
