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

#include "model/model_stack.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include "playback/playback_handler.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

using namespace deluge::modulation::params;

// Takes the NoteRow's *index*, not id!
// NoteRow must have a paramManager.
ModelStackWithThreeMainThings* ModelStackWithTimelineCounter::addNoteRowAndExtraStuff(int32_t noteRowIndex,
                                                                                      NoteRow* newNoteRow) const {

#if ALPHA_OR_BETA_VERSION
	if (!newNoteRow->paramManager.containsAnyParamCollectionsIncludingExpression()) {
		FREEZE_WITH_ERROR("E389");
	}
#endif

	ModelStackWithThreeMainThings* toReturn = (ModelStackWithThreeMainThings*)this;

	InstrumentClip* clip = (InstrumentClip*)getTimelineCounter();
	Output* output = clip->output;
	bool isKit = (output->type == OutputType::KIT);
	toReturn->noteRowId = isKit ? noteRowIndex : newNoteRow->y;
	toReturn->setNoteRow(newNoteRow);
	toReturn->modControllable =
	    (isKit && newNoteRow->drum) ? newNoteRow->drum->toModControllable() : output->toModControllable();
	toReturn->paramManager = &newNoteRow->paramManager;
	return toReturn;
}

// This could set a NULL NoteRow if it's not found. This would hopefully get picked up on call to getNoteRow(), which
// checks.
ModelStackWithNoteRow* ModelStackWithNoteRowId::automaticallyAddNoteRowFromId() const {
	InstrumentClip* clip = (InstrumentClip*)getTimelineCounter();
	NoteRow* noteRow = clip->getNoteRowFromId(noteRowId);

	ModelStackWithNoteRow* toReturn = (ModelStackWithNoteRow*)this;
	toReturn->setNoteRow(noteRow);
	return toReturn;
}

bool ModelStackWithNoteRow::isCurrentlyPlayingReversed() const {

	// Under a few different conditions, we just use the parent Clip's reversing status.
	if (!noteRow
	    || (noteRow->sequenceDirectionMode == SequenceDirection::OBEY_PARENT
	        && (!noteRow->loopLengthIfIndependent
	            || ((Clip*)getTimelineCounter())->sequenceDirectionMode != SequenceDirection::PINGPONG))) {
		return ((Clip*)getTimelineCounter())->currentlyPlayingReversed;
	}

	// Otherwise, we use the NoteRow's local one.
	else {
		return noteRow->currentlyPlayingReversedIfIndependent;
	}
}

int32_t ModelStackWithNoteRow::getLoopLength() const {
	if (noteRow && noteRow->loopLengthIfIndependent) {
		return noteRow->loopLengthIfIndependent;
	}
	else {
		return getTimelineCounter()->getLoopLength();
	}
}

int32_t ModelStackWithNoteRow::getLastProcessedPos() const {

	if (noteRow && noteRow->hasIndependentPlayPos()) {
		return noteRow->lastProcessedPosIfIndependent;
		// I have a feeling I should sort of be taking noteRowsNumTicksBehindClip into account here - but I know it's
		// usually zero when this gets called, and perhaps the other times I want to ignore it? Should probably
		// investigate further.
	}
	else {
		return getTimelineCounter()->getLastProcessedPos();
	}
}

int32_t ModelStackWithNoteRow::getRepeatCount() const {
	if (noteRow && noteRow->hasIndependentPlayPos()) {
		return noteRow->repeatCountIfIndependent;
	}
	else {
		return ((Clip*)getTimelineCounter())->repeatCount;
	}
}

ModelStackWithThreeMainThings* getModelStackFromSoundDrum(void* memory, SoundDrum* soundDrum) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	int32_t noteRowIndex;
	NoteRow* noteRow = clip->getNoteRowForDrum(soundDrum, &noteRowIndex);
	return setupModelStackWithThreeMainThingsIncludingNoteRow(memory, currentSong, getCurrentClip(), noteRowIndex,
	                                                          noteRow, soundDrum, &noteRow->paramManager);
}

// That's *cut* - as in, cut out abruptly. If it's looping, and the user isn't stopping it, that's not a cut.
// A cut could be if the Session-Clip is armed to stop, or if we're getting to the end of a ClipInstance in Arranger
int32_t ModelStackWithNoteRow::getPosAtWhichPlaybackWillCut() const {
	if (noteRow && noteRow->hasIndependentPlayPos()) {
		if (currentPlaybackMode == &session) {

			// Might need Arrangement-recording logic here, like in Session::getPosAtWhichClipWillCut()...
			// FYI that function's code basically mirrors this one - you should look at them together.

			bool reversed = isCurrentlyPlayingReversed();

			int32_t cutPos;

			if (session.willClipContinuePlayingAtEnd(toWithTimelineCounter())) {
				cutPos = reversed ? (-2147483648) : 2147483647; // If it's gonna loop, it's not gonna cut
			}

			else {
				int32_t ticksTilLaunchEvent =
				    session.launchEventAtSwungTickCount - playbackHandler.lastSwungTickActioned;
				if (reversed) {
					ticksTilLaunchEvent = -ticksTilLaunchEvent;
				}
				cutPos =
				    noteRow->lastProcessedPosIfIndependent
				    + ticksTilLaunchEvent; // Might return a pos beyond the loop length - maybe that's what we want?
			}

			// If pingponging, that's actually going to get referred to as a cut.
			if (noteRow->getEffectiveSequenceDirectionMode(this) == SequenceDirection::PINGPONG) {
				if (reversed) {
					if (cutPos < 0) {
						cutPos = noteRow->lastProcessedPosIfIndependent
						             ? 0
						             : -getLoopLength(); // Check we're not right at pos 0, as we briefly will be when
						                                 // we pingpong at the right-hand end of the Clip/etc.
					}
				}
				else {
					int32_t loopLength = getLoopLength();
					if (cutPos > loopLength) {
						cutPos = loopLength;
					}
				}
			}

			return cutPos;
		}
		else {
			// TODO: this
			return 2147483647;
		}
	}
	else {
		return getTimelineCounter()->getPosAtWhichPlaybackWillCut(toWithTimelineCounter());
	}
}

int32_t ModelStackWithNoteRow::getLivePos() const {
	if (noteRow) {
		return noteRow->getLivePos(this);
	}
	else {
		return getTimelineCounter()->getLivePos();
	}
}

// You must first be sure that noteRow is set, and has a ParamManager
ModelStackWithThreeMainThings* ModelStackWithNoteRow::addOtherTwoThingsAutomaticallyGivenNoteRow() const {
	ModelStackWithThreeMainThings* toReturn = (ModelStackWithThreeMainThings*)this;
	NoteRow* noteRowHere = getNoteRow();
	InstrumentClip* clip = (InstrumentClip*)getTimelineCounter();
	toReturn->modControllable = (clip->output->type == OutputType::KIT && noteRowHere->drum) // What if there's no Drum?
	                                ? noteRowHere->drum->toModControllable()
	                                : clip->output->toModControllable();
	toReturn->paramManager = &noteRowHere->paramManager;
	return toReturn;
}

bool ModelStackWithParamId::isParam(Kind kind, ParamType id) {
	return paramCollection && paramCollection->getParamKind() == kind && paramId == id;
}

bool ModelStackWithSoundFlags::checkSourceEverActiveDisregardingMissingSample(int32_t s) {
	int32_t flagValue = soundFlags[SOUND_FLAG_SOURCE_0_ACTIVE_DISREGARDING_MISSING_SAMPLE + s];
	if (flagValue == FLAG_TBD) {
		flagValue = ((Sound*)modControllable)
		                ->isSourceActiveEverDisregardingMissingSample(s, (ParamManagerForTimeline*)paramManager);
		soundFlags[SOUND_FLAG_SOURCE_0_ACTIVE_DISREGARDING_MISSING_SAMPLE + s] = flagValue;
	}
	return flagValue;
}

bool ModelStackWithSoundFlags::checkSourceEverActive(int32_t s) {
	int32_t flagValue = soundFlags[SOUND_FLAG_SOURCE_0_ACTIVE + s];
	if (flagValue == FLAG_TBD) {
		flagValue = checkSourceEverActiveDisregardingMissingSample(s);
		if (flagValue) { // Does an &&
			Sound* sound = (Sound*)modControllable;
			flagValue =
			    sound->synthMode == SynthMode::FM
			    || (sound->sources[s].oscType != OscType::SAMPLE && sound->sources[s].oscType != OscType::WAVETABLE)
			    || sound->sources[s].hasAtLeastOneAudioFileLoaded();
		}
		soundFlags[SOUND_FLAG_SOURCE_0_ACTIVE + s] = flagValue;
	}
	return flagValue;
}

void copyModelStack(void* newMemory, void const* oldMemory, int32_t size) {
	memcpy(newMemory, oldMemory, size);
}

ModelStackWithAutoParam* ModelStackWithThreeMainThings::getUnpatchedAutoParamFromId(int32_t newParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	if (paramManager && paramManager->containsAnyParamCollectionsIncludingExpression()) {
		ParamCollectionSummary* summary = paramManager->getUnpatchedParamSetSummary();

		ModelStackWithParamId* modelStackWithParamId =
		    addParamCollectionAndId(summary->paramCollection, summary, newParamId);

		modelStackWithParam = summary->paramCollection->getAutoParamFromId(modelStackWithParamId, true);
	}
	return modelStackWithParam;
}

ModelStackWithAutoParam* ModelStackWithThreeMainThings::getPatchedAutoParamFromId(int32_t newParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	if (paramManager && paramManager->containsAnyParamCollectionsIncludingExpression()) {
		ParamCollectionSummary* summary = paramManager->getPatchedParamSetSummary();

		ModelStackWithParamId* modelStackWithParamId =
		    addParamCollectionAndId(summary->paramCollection, summary, newParamId);

		modelStackWithParam = summary->paramCollection->getAutoParamFromId(modelStackWithParamId, true);
	}
	return modelStackWithParam;
}

ModelStackWithAutoParam* ModelStackWithThreeMainThings::getPatchCableAutoParamFromId(int32_t newParamId) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;
	if (paramManager && paramManager->containsAnyParamCollectionsIncludingExpression()) {
		ParamCollectionSummary* summary = paramManager->getPatchCableSetSummary();

		ModelStackWithParamId* modelStackWithParamId =
		    addParamCollectionAndId(summary->paramCollection, summary, newParamId);

		modelStackWithParam = summary->paramCollection->getAutoParamFromId(modelStackWithParamId, true);
	}
	return modelStackWithParam;
}

ModelStackWithAutoParam* ModelStackWithThreeMainThings::getExpressionAutoParamFromID(int32_t newParamId) {
	if (newParamId >= kNumExpressionDimensions) {
		return addParamCollectionAndId(nullptr, nullptr, 0)->addAutoParam(nullptr); // "No param"
	}

	paramManager->ensureExpressionParamSetExists(); // Allowed to fail
	ParamCollectionSummary* summary = paramManager->getExpressionParamSetSummary();
	ModelStackWithParamId* modelStackWithParamId =
	    addParamCollectionAndId(summary->paramCollection, summary, newParamId);

	return summary->paramCollection->getAutoParamFromId(modelStackWithParamId, true);
}
