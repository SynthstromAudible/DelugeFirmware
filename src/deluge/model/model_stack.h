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

#pragma once

#include "hid/display/display.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"

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
class SoundDrum;

/* ====================== ModelStacks =====================
 *
 * This is a system that helps each function keep track of the “things” (objects) it’s dealing with while it runs.
 * These “things” often include the Song, the Clip, the NoteRow - that sort of thing.
 * This was only introduced into the Deluge’s codebase only in 2020 - some functions do not (yet) use it.
 * Its inclusion has been beneficial to the codebase’s ease-of-modification, as well as code tidiness,
 * and probably a very slight performance improvement.
 *
 * Previously, the Deluge’s functions had to be passed these individual “things” as arguments
 * - a function might need to be passed a Clip and an AutoParam, say.
 * However, if I later decided that a function needed additional access - say to the relevant ParamCollection,
 * this could be tiresome to change, since the function’s caller might not have this,
 * so its caller would have to pass it through, but that caller might not have it either - etc.
 * Also, all this passing of arguments can’t be good for the compiled code’s efficiency and RAM / stack / register
 * usage.
 *
 * Another option would be for each “thing”, as stored in memory to include a pointer to its “parent” object.
 * E.g. each Clip would contain a pointer back to the Song, so that any function dealing with the
 * Clip could also find the Song. However, this would be unsatisfactory and inefficient because RAM
 * storage and access would be being used for something which theoretically the code should just be able to “know”.
 *
 * Enter my (Rohan’s) own invented solution, “ModelStacks” - a “stack” of the relevant parts of the “model”
 * (objects representing the makeup of a project on the Deluge) which the currently executing functions are dealing
 * with. Things can be “pushed and popped” (though the implementation doesn’t quite put it that way) onto and off the
 * ModelStack as needed. Now all that needs to be passed between functions is the pointer to the ModelStack - no other
 * memory or pointers need copying (except in special cases), and no additional arguments need to be passed. The
 * ModelStack typically exists in program stack memory.
 *
 * For example, suppose a Song needs to call a function on all Clips. The ModelStack begins by containing just the Song.
 * Then as each Clip has its function called, that Clip is set on the ModelStack.
 * And suppose each Clip then needs to call a function on its ParamManager - that’s pushed onto the ModelStack too.
 * So now, if the ParamManager, or anything else lower-level, needs access to the Song or Clip,
 * it’s right there on the ModelStack. The code now just “knows” what this stuff is, which I consider to be the
 * way it “should” be: a human reading / debugging / understanding the code will know what these higher-up objects are,
 * so why shouldn’t the code also have an intrinsic way to “know”?
 *
 * This is additionally beneficial because, suppose we decide at some future point that there needs to be
 * some new object inserted between Songs and Clips - maybe each Clip now belongs to a ClipGroup. We can
 * now mandate that the addClip() call is only available on a newly implemented ModelStackWithClipGroup,
 * for which having a ClipGroup is now a prerequisite. By simply trying to compile the code, the compiler
 * will generate errors, showing us everywhere that needs to be modified to add a relevant ClipGroup to the ModelStack
 * - still a bit of a task, but far easier as it will only be functions at higher-up levels that need to add
 * the ClipGroup, and then we can just take it for granted that it’s there in the ModelStack. The alternative would
 * be having to modify many functions all the way down the “tree” of the object / model structure, to accept a
 * ClipGroup as an argument, so that it can be passed down to the next thing / object.
 *
 * Another advantage is that error checking can be built into the ModelStack - which may also be easily
 * switched off for certain builds. For example, there are many instances in the Deluge codebase where
 * ModelStackWithTimelineCounter::getTimelineCounter() is called - usually to get the Clip
 * (TimelineCounter is a base class of Clip). We know that the returned TimelineCounter is not allowed to be NULL.
 * Rather than insert error checking into every instance of such a call to ensure that it wasn’t passed a NULL,
 * we can instead have getTimelineCounter() itself perform the check for us and generate an error if need be,
 * all in a single line of code.
 *
 * One disadvantage is that some simple function calls on a “leaf” / low-level object such as AutoParam
 * now require an entire ModelStack to be built up and provided, even if the function only in fact needed
 * to know about one parent object - e.g. the Clip. However, in practice, I’ve observed very few cases
 * where ModelStacks get populated unnecessarily - especially as ModelStacks are implemented more widely
 * throughout the codebase, so most functions already have a relevant ModelStack to pass further down the line.
 *
 * Another potential pitfall - suppose a “leaf” / low-level object - say AutoParam - needs to call a function
 * on its parent ParamCollection. In this sort of case, which is very common too, the ModelStack is passed back
 * upwards in the “tree hierarchy”. But now, what if this function in ParamCollection now needs to do something
 * that requires calling a function on each of its AutoParams? If it sets the AutoParam on the ModelStack, then
 * the original AutoParam - to which execution will eventually be returned - is no longer there on the ModelStack,
 * which may break things and we might not realise as we write the code. Ideally, I wish there was a solution
 * where we know that so long as the code compiles, we’re not at risk of overwriting anything on the ModelStack
 * that might be needed. I couldn’t devise a nice solution to this other than just exercising caution as the programmer.
 * My memory doesn’t quite serve me here - I experimented with having functions only accept a const ModelStack*
 * (which is now the case for many of the functions and I can’t quite remember why, sorry!) but this somehow did not
 * provide a solution for the code to be immune to the pitfall identified above.
 *
 * I’m actually not sure how fields like game development deal with similar problems, which they must
 * encounter as e.g. a “world” might contain many “levels”, which might also contain many “enemies”
 * - a tree-like structure which the code must have to traverse, like on the Deluge. I tried Googling it,
 * but couldn’t find anything about a standard approach to this. Perhaps the each-object-stores-a-pointer-to-its-parent
 * solution, as I mentioned above, is the norm? If you know, I’d be really interested to know!
 */

class ModelStack {
public:
	Song* song;
	ModelStackWithTimelineCounter* addTimelineCounter(TimelineCounter* newTimelineCounter) const;
};

class ModelStackWithTimelineCounter {
public:
	Song* song;

	ModelStackWithNoteRow* addNoteRow(int32_t noteRowId, NoteRow* noteRow) const;
	ModelStackWithNoteRowId* addNoteRowId(int32_t noteRowId) const;
	ModelStackWithThreeMainThings* addNoteRowAndExtraStuff(int32_t noteRowIndex, NoteRow* newNoteRow) const;
	ModelStackWithThreeMainThings* addOtherTwoThingsButNoNoteRow(ModControllable* newModControllable,
	                                                             ParamManager* newParamManager) const;
	ModelStackWithModControllable* addModControllableButNoNoteRow(ModControllable* newModControllable) const;

	inline ModelStack*
	toWithSong() const { // I thiiiink you're supposed to just be real careful about when you call this etc...
		return (ModelStack*)this;
	}

	inline bool timelineCounterIsSet() const { return timelineCounter; }

	inline TimelineCounter* getTimelineCounter() const {
#if ALPHA_OR_BETA_VERSION
		if (!timelineCounter) {
			FREEZE_WITH_ERROR("E369");
		}
#endif
		return timelineCounter;
	}

	inline TimelineCounter* getTimelineCounterAllowNull() const { return timelineCounter; }

	inline void setTimelineCounter(TimelineCounter* newTimelineCounter) { timelineCounter = newTimelineCounter; }

protected:
	TimelineCounter* timelineCounter; // Allowed to be NULL
};

class ModelStackWithNoteRowId {
public:
	Song* song;

protected:
	TimelineCounter* timelineCounter; // Allowed to be NULL
public:
	inline ModelStackWithTimelineCounter* toWithTimelineCounter() const { return (ModelStackWithTimelineCounter*)this; }

	inline TimelineCounter* getTimelineCounter() const { return toWithTimelineCounter()->getTimelineCounter(); }

	inline TimelineCounter* getTimelineCounterAllowNull() const {
		return toWithTimelineCounter()->getTimelineCounterAllowNull();
	}

	inline void setTimelineCounter(TimelineCounter* newTimelineCounter) {
		toWithTimelineCounter()->setTimelineCounter(newTimelineCounter);
	}

	inline bool timelineCounterIsSet() const { return toWithTimelineCounter()->timelineCounterIsSet(); }

	ModelStackWithNoteRow* automaticallyAddNoteRowFromId() const;
	int32_t noteRowId; // Valid and mandatory, iff noteRow is set
};

class ModelStackWithNoteRow : public ModelStackWithNoteRowId {
public:
	inline void setNoteRow(NoteRow* newNoteRow, int32_t newNoteRowId) {
		noteRow = newNoteRow;
		noteRowId = newNoteRowId;
	}

	inline NoteRow* getNoteRow() const {
#if ALPHA_OR_BETA_VERSION
		if (!noteRow) {
			FREEZE_WITH_ERROR("E379");
		}
#endif
		return noteRow;
	}

	inline NoteRow* getNoteRowAllowNull() const { return noteRow; }

	inline void setNoteRow(NoteRow* newNoteRow) { noteRow = newNoteRow; }

	ModelStackWithThreeMainThings* addOtherTwoThings(ModControllable* newModControllable,
	                                                 ParamManager* newParamManager) const;
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

class ModelStackWithModControllable : public ModelStackWithNoteRow {
public:
	ModControllable* modControllable;
	ModelStackWithThreeMainThings* addParamManager(ParamManagerForTimeline* newParamManager) const;
};

class ModelStackWithThreeMainThings : public ModelStackWithModControllable {
public:
	ParamManager* paramManager;

	ModelStackWithParamCollection* addParamCollection(ParamCollection* newParamCollection,
	                                                  ParamCollectionSummary* newSummary) const;
	ModelStackWithParamCollection* addParamCollectionSummary(ParamCollectionSummary* newSummary) const;
	ModelStackWithParamId* addParamCollectionAndId(ParamCollection* newParamCollection,
	                                               ParamCollectionSummary* newSummary, int32_t newParamId) const;
	ModelStackWithAutoParam* addParam(ParamCollection* newParamCollection, ParamCollectionSummary* newSummary,
	                                  int32_t newParamId, AutoParam* newAutoParam) const;
	ModelStackWithAutoParam* getUnpatchedAutoParamFromId(int32_t newParamId);
	ModelStackWithAutoParam* getPatchedAutoParamFromId(int32_t newParamId);
	ModelStackWithAutoParam* getPatchCableAutoParamFromId(int32_t newParamId);

	inline ModelStackWithSoundFlags* addSoundFlags() const;
	inline ModelStackWithSoundFlags* addDummySoundFlags() const;
	ModelStackWithAutoParam* getExpressionAutoParamFromID(int32_t newParamId);
};

class ModelStackWithParamCollection : public ModelStackWithThreeMainThings {
public:
	ParamCollection* paramCollection;
	ParamCollectionSummary* summary;

	ModelStackWithParamId* addParamId(int32_t newParamId) const;
	ModelStackWithAutoParam* addAutoParam(int32_t newParamId, AutoParam* newAutoParam) const;
};

class ModelStackWithParamId : public ModelStackWithParamCollection {
public:
	int32_t paramId;

	ModelStackWithAutoParam* addAutoParam(AutoParam* newAutoParam) const;

	bool isParam(deluge::modulation::params::Kind kind, deluge::modulation::params::ParamType id);
};

class ModelStackWithAutoParam : public ModelStackWithParamId {
public:
	/// AutoParam attached to the ParamID. If this is null, none of the other param related members can be trusted
	/// (e.g. the paramcollection, summary, or paramId)
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

class ModelStackWithSoundFlags : public ModelStackWithThreeMainThings {
public:
	uint8_t soundFlags[NUM_SOUND_FLAGS];

	ModelStackWithVoice* addVoice(Voice* voice) const;
	bool checkSourceEverActiveDisregardingMissingSample(int32_t s);
	bool checkSourceEverActive(int32_t s);
};

class ModelStackWithVoice : public ModelStackWithSoundFlags {
public:
	Voice* voice;
};

#define MODEL_STACK_MAX_SIZE sizeof(ModelStackWithAutoParam)

ModelStackWithThreeMainThings* getModelStackFromSoundDrum(void* memory, SoundDrum* soundDrum);

inline ModelStack* setupModelStackWithSong(void* memory, Song* newSong) {
	ModelStack* modelStack = (ModelStack*)memory;
	modelStack->song = newSong;

	return modelStack;
}

inline ModelStackWithTimelineCounter* setupModelStackWithTimelineCounter(void* memory, Song* newSong,
                                                                         TimelineCounter* newTimelineCounter) {
	ModelStackWithTimelineCounter* modelStack = (ModelStackWithTimelineCounter*)memory;
	modelStack->song = newSong;
	modelStack->setTimelineCounter(newTimelineCounter);

	return modelStack;
}

inline ModelStackWithModControllable* setupModelStackWithModControllable(void* memory, Song* newSong,
                                                                         TimelineCounter* newTimelineCounter,
                                                                         ModControllable* newModControllable) {

	return setupModelStackWithSong(memory, newSong)
	    ->addTimelineCounter(newTimelineCounter)
	    ->addNoteRow(0, NULL)
	    ->addModControllable(newModControllable);
}

inline ModelStackWithThreeMainThings*
setupModelStackWithThreeMainThingsButNoNoteRow(void* memory, Song* newSong, ModControllable* newModControllable,
                                               TimelineCounter* newTimelineCounter, ParamManager* newParamManager) {

	return setupModelStackWithSong(memory, newSong)
	    ->addTimelineCounter(newTimelineCounter)
	    ->addNoteRow(0, NULL)
	    ->addOtherTwoThings(newModControllable, newParamManager);
}

inline ModelStackWithThreeMainThings* setupModelStackWithThreeMainThingsIncludingNoteRow(
    void* memory, Song* newSong, TimelineCounter* newTimelineCounter, int32_t noteRowId, NoteRow* noteRow,
    ModControllable* newModControllable, ParamManagerForTimeline* newParamManager) {

	return setupModelStackWithSong(memory, newSong)
	    ->addTimelineCounter(newTimelineCounter)
	    ->addNoteRow(noteRowId, noteRow)
	    ->addOtherTwoThings(newModControllable, newParamManager);
}

inline ModelStackWithTimelineCounter* ModelStack::addTimelineCounter(TimelineCounter* newTimelineCounter) const {
	ModelStackWithTimelineCounter* toReturn = (ModelStackWithTimelineCounter*)this;
	toReturn->setTimelineCounter(newTimelineCounter);
	return toReturn;
}

inline ModelStackWithNoteRowId* ModelStackWithTimelineCounter::addNoteRowId(int32_t noteRowId) const {
	ModelStackWithNoteRowId* toReturn = (ModelStackWithNoteRowId*)this;
	toReturn->noteRowId = noteRowId;
	return toReturn;
}

inline ModelStackWithNoteRow* ModelStackWithTimelineCounter::addNoteRow(int32_t noteRowId, NoteRow* noteRow) const {
	ModelStackWithNoteRow* toReturn = (ModelStackWithNoteRow*)this;
	toReturn->noteRowId = noteRowId;
	toReturn->setNoteRow(noteRow);
	return toReturn;
}

inline ModelStackWithModControllable*
ModelStackWithTimelineCounter::addModControllableButNoNoteRow(ModControllable* newModControllable) const {
	return addNoteRow(0, NULL)->addModControllable(newModControllable);
}

inline ModelStackWithThreeMainThings*
ModelStackWithTimelineCounter::addOtherTwoThingsButNoNoteRow(ModControllable* newModControllable,
                                                             ParamManager* newParamManager) const {
	return addNoteRow(0, NULL)->addOtherTwoThings(newModControllable, newParamManager);
}

inline ModelStackWithModControllable*
ModelStackWithNoteRow::addModControllable(ModControllable* newModControllable) const {
	ModelStackWithModControllable* toReturn = (ModelStackWithModControllable*)this;
	toReturn->modControllable = newModControllable;
	return toReturn;
}

/**
 * adds a modcontrollable and a param manager
 */
inline ModelStackWithThreeMainThings* ModelStackWithNoteRow::addOtherTwoThings(ModControllable* newModControllable,
                                                                               ParamManager* newParamManager) const {
	ModelStackWithThreeMainThings* toReturn = (ModelStackWithThreeMainThings*)this;
	toReturn->modControllable = newModControllable;
	toReturn->paramManager = newParamManager;
	return toReturn;
}

inline ModelStackWithThreeMainThings*
ModelStackWithModControllable::addParamManager(ParamManagerForTimeline* newParamManager) const {
	ModelStackWithThreeMainThings* toReturn = (ModelStackWithThreeMainThings*)this;
	toReturn->paramManager = newParamManager;
	return toReturn;
}

// Although the ParamCollection is referenced inside the Summary, this is to call when you've already grabbed that
// pointer out, to avoid the CPU having to go and look at it again.
inline ModelStackWithParamCollection*
ModelStackWithThreeMainThings::addParamCollection(ParamCollection* newParamCollection,
                                                  ParamCollectionSummary* newSummary) const {
	ModelStackWithParamCollection* toReturn = (ModelStackWithParamCollection*)this;
	toReturn->paramCollection = newParamCollection;
	toReturn->summary = newSummary;
	return toReturn;
}

// To call when you haven't already separately grabbed the paramCollection pointer out - for convenience.
inline ModelStackWithParamCollection*
ModelStackWithThreeMainThings::addParamCollectionSummary(ParamCollectionSummary* newSummary) const {
	ModelStackWithParamCollection* toReturn = (ModelStackWithParamCollection*)this;
	toReturn->summary = newSummary;
	toReturn->paramCollection = newSummary->paramCollection;
	return toReturn;
}

inline ModelStackWithParamId*
ModelStackWithThreeMainThings::addParamCollectionAndId(ParamCollection* newParamCollection,
                                                       ParamCollectionSummary* newSummary, int32_t newParamId) const {
	ModelStackWithParamId* toReturn = (ModelStackWithParamId*)this;
	toReturn->paramCollection = newParamCollection;
	toReturn->summary = newSummary;
	toReturn->paramId = newParamId;
	return toReturn;
}

inline ModelStackWithAutoParam* ModelStackWithThreeMainThings::addParam(ParamCollection* newParamCollection,
                                                                        ParamCollectionSummary* newSummary,
                                                                        int32_t newParamId,
                                                                        AutoParam* newAutoParam) const {
	ModelStackWithAutoParam* toReturn = (ModelStackWithAutoParam*)this;
	toReturn->paramCollection = newParamCollection;
	toReturn->summary = newSummary;
	toReturn->paramId = newParamId;
	toReturn->autoParam = newAutoParam;
	return toReturn;
}

inline ModelStackWithParamId* ModelStackWithParamCollection::addParamId(int32_t newParamId) const {
	ModelStackWithParamId* toReturn = (ModelStackWithParamId*)this;
	toReturn->paramId = newParamId;
	return toReturn;
}

inline ModelStackWithAutoParam* ModelStackWithParamCollection::addAutoParam(int32_t newParamId,
                                                                            AutoParam* newAutoParam) const {
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
	for (int32_t i = 0; i < NUM_SOUND_FLAGS; i++) {
		toReturn->soundFlags[i] = FLAG_TBD;
	}
	return toReturn;
}

inline ModelStackWithSoundFlags* ModelStackWithThreeMainThings::addDummySoundFlags() const {
	ModelStackWithSoundFlags* toReturn = (ModelStackWithSoundFlags*)this;
#if ALPHA_OR_BETA_VERSION
	for (int32_t i = i; i < NUM_SOUND_FLAGS; i++) {
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

void copyModelStack(void* newMemory, void const* oldMemory, int32_t size);

/*

char modelStackMemory[MODEL_STACK_MAX_SIZE];
ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

*/

/*
    char modelStackMemory[MODEL_STACK_MAX_SIZE];
    ModelStack* modelStack = setupModelStackWithSong(modelStackMemory, song);
    ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(clip);


    char modelStackMemory[MODEL_STACK_MAX_SIZE];
    ModelStackWithTimelineCounter* modelStack = setupModelStackWithSong(modelStackMemory,
   currentSong)->addTimelineCounter(clip);


 */
