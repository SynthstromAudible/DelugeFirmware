/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#ifndef MODELSTACK_H_
#define MODELSTACK_H_

#include <ParamManager.h>
#include "numericdriver.h"

class Song;
class ModControllable;
class TimelineCounter;
class ParamManagerForTimeline;
class ParamCollection;
class AutoParam;
class ModelStackWithAutoParam;
class ModelStackWithParamId;
class ModelStackWithParamCollection;
class ModelStackWithThreeMainThings;
class ModelStackWithTimelineCounter;
class NoteRow;
class ModelStackWithNoteRow;
class ModelStackWithNoteRowId;
class ModelStackWithModControllable;
class ModelStackWithVoice;
class Voice;
class ModelStackWithSoundFlags;
class ParamManager;
class ParamCollectionSummary;

class ModelStack {
public:
	Song* song;
	ModelStackWithTimelineCounter* addTimelineCounter(TimelineCounter* newTimelineCounter) const;
};


class ModelStackWithTimelineCounter {
public:
	Song* song;

	ModelStackWithNoteRow* addNoteRow(int noteRowId, NoteRow* noteRow) const;
	ModelStackWithNoteRowId* addNoteRowId(int noteRowId) const;
	ModelStackWithThreeMainThings* addNoteRowAndExtraStuff(int noteRowIndex, NoteRow* newNoteRow) const;
	ModelStackWithThreeMainThings* addOtherTwoThingsButNoNoteRow(ModControllable* newModControllable, ParamManager* newParamManager) const;
	ModelStackWithModControllable* addModControllableButNoNoteRow(ModControllable* newModControllable) const;

	inline ModelStack* toWithSong() const { // I thiiiink you're supposed to just be real careful about when you call this etc...
		return (ModelStack*)this;
	}

	inline bool timelineCounterIsSet() const {
		return timelineCounter;
	}

	inline TimelineCounter* getTimelineCounter() const {
#if ALPHA_OR_BETA_VERSION
		if (!timelineCounter) numericDriver.freezeWithError("E369");
#endif
		return timelineCounter;
	}

	inline TimelineCounter* getTimelineCounterAllowNull() const {
		return timelineCounter;
	}

	inline void setTimelineCounter(TimelineCounter* newTimelineCounter) {
		timelineCounter = newTimelineCounter;
	}

protected:
	TimelineCounter* timelineCounter; // Allowed to be NULL
};

class ModelStackWithNoteRowId {
public:
	Song* song;
protected:
	TimelineCounter* timelineCounter; // Allowed to be NULL
public:
	inline ModelStackWithTimelineCounter* toWithTimelineCounter() const {
		return (ModelStackWithTimelineCounter*)this;
	}

	inline TimelineCounter* getTimelineCounter() const {
		return toWithTimelineCounter()->getTimelineCounter();
	}

	inline TimelineCounter* getTimelineCounterAllowNull() const {
		return toWithTimelineCounter()->getTimelineCounterAllowNull();
	}

	inline void setTimelineCounter(TimelineCounter* newTimelineCounter) {
		toWithTimelineCounter()->setTimelineCounter(newTimelineCounter);
	}

	inline bool timelineCounterIsSet() const {
		return toWithTimelineCounter()->timelineCounterIsSet();
	}


	ModelStackWithNoteRow* automaticallyAddNoteRowFromId() const;
	int noteRowId; // Valid and mandatory, iff noteRow is set
};


class ModelStackWithNoteRow: public ModelStackWithNoteRowId {
public:
	inline void setNoteRow(NoteRow* newNoteRow, int newNoteRowId) {
		noteRow = newNoteRow;
		noteRowId = newNoteRowId;
	}

	inline NoteRow* getNoteRow() const {
#if ALPHA_OR_BETA_VERSION
		if (!noteRow) numericDriver.freezeWithError("E379");
#endif
		return noteRow;
	}

	inline NoteRow* getNoteRowAllowNull() const {
		return noteRow;
	}

	inline void setNoteRow(NoteRow* newNoteRow) {
		noteRow = newNoteRow;
	}

	ModelStackWithThreeMainThings* addOtherTwoThings(ModControllable* newModControllable, ParamManager* newParamManager) const;
	ModelStackWithModControllable* addModControllable(ModControllable* newModControllable) const;
	ModelStackWithThreeMainThings* addOtherTwoThingsAutomaticallyGivenNoteRow() const;

	int32_t getLoopLength() const;
	int32_t getRepeatCount() const;
	int32_t getLastProcessedPos() const;
	int32_t getLivePos() const;
	bool isCurrentlyPlayingReversed() const;
	int32_t getPosAtWhichPlaybackWillCut() const;

protected:
	NoteRow* noteRow; // Very often will be NULL
};

class ModelStackWithModControllable: public ModelStackWithNoteRow {
public:
	ModControllable* modControllable;
	ModelStackWithThreeMainThings* addParamManager(ParamManagerForTimeline* newParamManager) const;

};

class ModelStackWithThreeMainThings: public ModelStackWithModControllable {
public:
	ParamManager* paramManager;

	ModelStackWithParamCollection* addParamCollection(ParamCollection* newParamCollection, ParamCollectionSummary* newSummary) const;
	ModelStackWithParamCollection* addParamCollectionSummary(ParamCollectionSummary* newSummary) const;
	ModelStackWithParamId* addParamCollectionAndId(ParamCollection* newParamCollection, ParamCollectionSummary* newSummary, int newParamId) const;
	ModelStackWithAutoParam* addParam(ParamCollection* newParamCollection, ParamCollectionSummary* newSummary, int newParamId, AutoParam* newAutoParam) const;

	inline ModelStackWithSoundFlags* addSoundFlags() const;
	inline ModelStackWithSoundFlags* addDummySoundFlags() const;

};


class ModelStackWithParamCollection: public ModelStackWithThreeMainThings {
public:
	ParamCollection* paramCollection;
	ParamCollectionSummary* summary;

	ModelStackWithParamId* addParamId(int newParamId) const;
	ModelStackWithAutoParam* addAutoParam(int newParamId, AutoParam* newAutoParam) const;
};


class ModelStackWithParamId: public ModelStackWithParamCollection {
public:
	int paramId;

	ModelStackWithAutoParam* addAutoParam(AutoParam* newAutoParam) const;
};



class ModelStackWithAutoParam: public ModelStackWithParamId {
public:
	AutoParam* autoParam;
};


#define SOUND_FLAG_SOURCE_0_ACTIVE_DISREGARDING_MISSING_SAMPLE 0
#define SOUND_FLAG_SOURCE_1_ACTIVE_DISREGARDING_MISSING_SAMPLE 1
#define SOUND_FLAG_SOURCE_0_ACTIVE 2
#define SOUND_FLAG_SOURCE_1_ACTIVE 3
#define NUM_SOUND_FLAGS 4

#define FLAG_FALSE 0
#define FLAG_TRUE 1
#define FLAG_TBD 2
#define FLAG_SHOULDNT_BE_NEEDED 3


class ModelStackWithSoundFlags: public ModelStackWithThreeMainThings {
public:
	uint8_t soundFlags[NUM_SOUND_FLAGS];

	ModelStackWithVoice* addVoice(Voice* voice) const;
	bool checkSourceEverActiveDisregardingMissingSample(int s);
	bool checkSourceEverActive(int s);
};


class ModelStackWithVoice: public ModelStackWithSoundFlags {
public:
	Voice* voice;
};

#define MODEL_STACK_MAX_SIZE sizeof(ModelStackWithAutoParam)












inline ModelStack* setupModelStackWithSong(void* memory, Song* newSong) {
	ModelStack* modelStack = (ModelStack*)memory;
	modelStack->song = newSong;

	return modelStack;
}

inline ModelStackWithTimelineCounter* setupModelStackWithTimelineCounter(void* memory, Song* newSong, TimelineCounter* newTimelineCounter) {
	ModelStackWithTimelineCounter* modelStack = (ModelStackWithTimelineCounter*)memory;
	modelStack->song = newSong;
	modelStack->setTimelineCounter(newTimelineCounter);

	return modelStack;
}

inline ModelStackWithModControllable* setupModelStackWithModControllable(void* memory, Song* newSong, TimelineCounter* newTimelineCounter, ModControllable* newModControllable) {

	return setupModelStackWithSong(memory, newSong)->addTimelineCounter(newTimelineCounter)->addNoteRow(0, NULL)->addModControllable(newModControllable);
}

inline ModelStackWithThreeMainThings* setupModelStackWithThreeMainThingsButNoNoteRow(void* memory, Song* newSong, ModControllable* newModControllable, TimelineCounter* newTimelineCounter, ParamManager* newParamManager) {

	return setupModelStackWithSong(memory, newSong)->addTimelineCounter(newTimelineCounter)->addNoteRow(0, NULL)->addOtherTwoThings(newModControllable, newParamManager);
}


inline ModelStackWithThreeMainThings* setupModelStackWithThreeMainThingsIncludingNoteRow(void* memory, Song* newSong, TimelineCounter* newTimelineCounter, int noteRowId, NoteRow* noteRow, ModControllable* newModControllable, ParamManagerForTimeline* newParamManager) {

	return setupModelStackWithSong(memory, newSong)->addTimelineCounter(newTimelineCounter)->addNoteRow(noteRowId, noteRow)->addOtherTwoThings(newModControllable, newParamManager);
}






inline ModelStackWithTimelineCounter* ModelStack::addTimelineCounter(TimelineCounter* newTimelineCounter) const {
	ModelStackWithTimelineCounter* toReturn = (ModelStackWithTimelineCounter*)this;
	toReturn->setTimelineCounter(newTimelineCounter);
	return toReturn;
}

inline ModelStackWithNoteRowId* ModelStackWithTimelineCounter::addNoteRowId(int noteRowId) const {
	ModelStackWithNoteRowId* toReturn = (ModelStackWithNoteRowId*)this;
	toReturn->noteRowId = noteRowId;
	return toReturn;
}

inline ModelStackWithNoteRow* ModelStackWithTimelineCounter::addNoteRow(int noteRowId, NoteRow* noteRow) const {
	ModelStackWithNoteRow* toReturn = (ModelStackWithNoteRow*)this;
	toReturn->noteRowId = noteRowId;
	toReturn->setNoteRow(noteRow);
	return toReturn;
}


inline ModelStackWithModControllable* ModelStackWithTimelineCounter::addModControllableButNoNoteRow(ModControllable* newModControllable) const {
	return addNoteRow(0, NULL)->addModControllable(newModControllable);
}


inline ModelStackWithThreeMainThings* ModelStackWithTimelineCounter::addOtherTwoThingsButNoNoteRow(ModControllable* newModControllable, ParamManager* newParamManager) const {
	return addNoteRow(0, NULL)->addOtherTwoThings(newModControllable, newParamManager);
}


inline ModelStackWithModControllable* ModelStackWithNoteRow::addModControllable(ModControllable* newModControllable) const {
	ModelStackWithModControllable* toReturn = (ModelStackWithModControllable*)this;
	toReturn->modControllable = newModControllable;
	return toReturn;
}


inline ModelStackWithThreeMainThings* ModelStackWithNoteRow::addOtherTwoThings(ModControllable* newModControllable, ParamManager* newParamManager) const {
	ModelStackWithThreeMainThings* toReturn = (ModelStackWithThreeMainThings*)this;
	toReturn->modControllable = newModControllable;
	toReturn->paramManager = newParamManager;
	return toReturn;
}


inline ModelStackWithThreeMainThings* ModelStackWithModControllable::addParamManager(ParamManagerForTimeline* newParamManager) const {
	ModelStackWithThreeMainThings* toReturn = (ModelStackWithThreeMainThings*)this;
	toReturn->paramManager = newParamManager;
	return toReturn;
}

// Although the ParamCollection is referenced inside the Summary, this is to call when you've already grabbed that pointer out, to avoid the CPU having to go and look at it again.
inline ModelStackWithParamCollection* ModelStackWithThreeMainThings::addParamCollection(ParamCollection* newParamCollection, ParamCollectionSummary* newSummary) const {
	ModelStackWithParamCollection* toReturn = (ModelStackWithParamCollection*)this;
	toReturn->paramCollection = newParamCollection;
	toReturn->summary = newSummary;
	return toReturn;
}

// To call when you haven't already separately grabbed the paramCollection pointer out - for convenience.
inline ModelStackWithParamCollection* ModelStackWithThreeMainThings::addParamCollectionSummary(ParamCollectionSummary* newSummary) const {
	ModelStackWithParamCollection* toReturn = (ModelStackWithParamCollection*)this;
	toReturn->summary = newSummary;
	toReturn->paramCollection = newSummary->paramCollection;
	return toReturn;
}

inline ModelStackWithParamId* ModelStackWithThreeMainThings::addParamCollectionAndId(ParamCollection* newParamCollection, ParamCollectionSummary* newSummary, int newParamId) const {
	ModelStackWithParamId* toReturn = (ModelStackWithParamId*)this;
	toReturn->paramCollection = newParamCollection;
	toReturn->summary = newSummary;
	toReturn->paramId = newParamId;
	return toReturn;
}

inline ModelStackWithAutoParam* ModelStackWithThreeMainThings::addParam(ParamCollection* newParamCollection, ParamCollectionSummary* newSummary, int newParamId, AutoParam* newAutoParam) const {
	ModelStackWithAutoParam* toReturn = (ModelStackWithAutoParam*)this;
	toReturn->paramCollection = newParamCollection;
	toReturn->summary = newSummary;
	toReturn->paramId = newParamId;
	toReturn->autoParam = newAutoParam;
	return toReturn;
}

inline ModelStackWithParamId* ModelStackWithParamCollection::addParamId(int newParamId) const {
	ModelStackWithParamId* toReturn = (ModelStackWithParamId*)this;
	toReturn->paramId = newParamId;
	return toReturn;
}

inline ModelStackWithAutoParam* ModelStackWithParamCollection::addAutoParam(int newParamId, AutoParam* newAutoParam) const {
	ModelStackWithAutoParam* toReturn = (ModelStackWithAutoParam*)this;
	toReturn->paramId = newParamId;
	toReturn->autoParam = newAutoParam;
	return toReturn;
}

inline ModelStackWithAutoParam* ModelStackWithParamId::addAutoParam(AutoParam* newAutoParam) const {
	ModelStackWithAutoParam* toReturn = (ModelStackWithAutoParam*)this;
	toReturn->autoParam = newAutoParam;
	return toReturn;
}

inline ModelStackWithSoundFlags* ModelStackWithThreeMainThings::addSoundFlags() const {
	ModelStackWithSoundFlags* toReturn = (ModelStackWithSoundFlags*)this;
	for (int i = 0; i < NUM_SOUND_FLAGS; i++) {
		toReturn->soundFlags[i] = FLAG_TBD;
	}
	return toReturn;
}

inline ModelStackWithSoundFlags* ModelStackWithThreeMainThings::addDummySoundFlags() const {
	ModelStackWithSoundFlags* toReturn = (ModelStackWithSoundFlags*)this;
#if ALPHA_OR_BETA_VERSION
	for (int i = i; i < NUM_SOUND_FLAGS; i++) {
		toReturn->soundFlags[i] = FLAG_SHOULDNT_BE_NEEDED;
	}
#endif
	return toReturn;
}


inline ModelStackWithVoice* ModelStackWithSoundFlags::addVoice(Voice* voice) const {
	ModelStackWithVoice* toReturn = (ModelStackWithVoice*)this;
	toReturn->voice = voice;
	return toReturn;
}

void copyModelStack(void* newMemory, void const* oldMemory, int size);

/*

char modelStackMemory[MODEL_STACK_MAX_SIZE];
ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

*/


/*
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, song);
	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);


	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = setupModelStackWithSong(modelStackMemory, currentSong)->addTimelineCounter(clip);


 */

#endif /* MODELSTACK_H_ */
