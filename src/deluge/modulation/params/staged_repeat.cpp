/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "modulation/params/staged_repeat.h"

#include "deluge-project/param_edit.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip_array.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/working_copies.h"
#include "modulation/params/lane_publication.h"
#include "modulation/params/lane_snapshot.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_collection_summary.h"
#include "modulation/params/param_manager.h"
#include "playback/playback_handler.h"

#include <atomic>

namespace deluge::modulation {

#if ALPHA_OR_BETA_VERSION
uint32_t stagedRepeatMisses = 0;
uint32_t stagedRepeatSwaps = 0;
uint32_t audioSpareMisses = 0;
#endif

namespace {
/// [render] or [interaction] set, [interaction] cleared: a repeat was counted since the last catch-up.
std::atomic<bool> g_repeatsPending{false};

/// The stacks of a take and its source, for its repeats.
struct TakeStacks {
	alignas(ModelStackWithAutoParam) char thisMemory[MODEL_STACK_MAX_SIZE];
	alignas(ModelStackWithAutoParam) char otherMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* take;
	ModelStackWithTimelineCounter* source;

	TakeStacks(Song& song, InstrumentClip& take, InstrumentClip& source)
	    : take(setupModelStackWithSong(thisMemory, &song)->addTimelineCounter(&take)),
	      source(setupModelStackWithSong(otherMemory, &song)->addTimelineCounter(&source)) {}
};

/// Call @p visit with each collection of @p mine a repeat appends to, the source's (@p source) paired one and its
/// summary: a take's collections and its source's, by their place in their managers, those of the take with
/// automation.
template <typename Visit>
void forEachAppendedCollection(ParamManagerForTimeline& mine, ParamManagerForTimeline& source, Visit visit) {
	for (size_t i = 0; i < std::size(mine.summaries); i++) {
		ParamCollectionSummary& summary = mine.summaries[i];
		ParamCollectionSummary& other = source.summaries[i];
		if (summary.paramCollection == nullptr || other.paramCollection == nullptr) {
			return;
		}
		if (summary.containsAutomation()) {
			visit(*summary.paramCollection, *other.paramCollection, other);
		}
	}
}

/// Call @p visit with each collection of @p manager.
template <typename Visit>
void forEachCollection(ParamManagerForTimeline& manager, Visit visit) {
	for (ParamCollectionSummary& summary : manager.summaries) {
		if (summary.paramCollection == nullptr) {
			return;
		}
		visit(*summary.paramCollection);
	}
}

/// A walk of the param managers a repeat at an offset appends to, each with the source's paired one and the append's
/// arguments: a take clip's (InstrumentClip::forEachRepeatedManager()) or a row's with its own length.
using ManagerVisit = deluge::function_ref<void(ParamManagerForTimeline&, ParamManagerForTimeline&, const RepeatArgs&)>;
using ManagerWalk = deluge::function_ref<void(int32_t, ManagerVisit)>;

/// The walk of @p row's one manager, a row with its own length: @p rowStack names it, @p otherRowStack its source's.
auto rowWalk(NoteRow& row, ModelStackWithNoteRow* rowStack, ModelStackWithNoteRow* otherRowStack) {
	return [&row, rowStack, otherRowStack](int32_t offset, ManagerVisit visit) {
		(void)rowStack;
		NoteRow* otherRow = otherRowStack->getNoteRow();
		const int32_t otherLength = otherRow->loopLengthIfIndependent();
		if (row.paramManager.containsAnyParamCollectionsIncludingExpression()
		    && otherRow->paramManager.containsAnyParamCollectionsIncludingExpression()) {
			visit(row.paramManager, otherRow->paramManager,
			      NoteRow::repeatArgs(otherRowStack, offset, offset / otherLength, otherLength));
		}
	};
}

/// Append the repeat numbered @p repeat, at @p offset, of each manager @p walk visits: into each lane's working copy
/// that does not hold it yet, else into the model's lane; the collections republish.
void appendRepeat(uint32_t repeat, int32_t offset, ManagerWalk walk) {
	walk(offset, [&](ParamManagerForTimeline& mine, ParamManagerForTimeline& source, const RepeatArgs& args) {
		forEachAppendedCollection(
		    mine, source,
		    [&](ParamCollection& collection, ParamCollection& other, const ParamCollectionSummary& otherSummary) {
			    collection.forEachAppendedKey(other, otherSummary, [&](int32_t key) {
				    const project::DocAutoParam* src = other.findDocParamForId(key);
				    if (appendRepeatToCopy(collection, key, repeat, src, args) || src == nullptr
				        || src->nodes.empty()) {
					    return;
				    }
				    if (project::DocAutoParam* lane = collection.laneToAppendTo(key)) {
					    project::appendParam(*lane, *src, args.offset, args.reverseThisRepeatWithLength,
					                         args.pingpongingGenerally);
				    }
			    });
			    markLanesChanged(collection);
			    collection.ticksTilNextEvent = 0;
		    });
		mine.ticksTilNextEvent = 0;
	});
}

/// Append the automation of @p take's repeats counted and not yet in the model: its own, and each row's with its own
/// length.
void catchUp(Song& song, InstrumentClip& take) {
	TakeRepeats& repeats = take.repeats;
	InstrumentClip* source = repeats.source.load(std::memory_order_acquire);
	bool whole = true;
	const auto drain = [&](TakeRepeats& r, auto append) {
		const uint32_t counted = r.appended.load(std::memory_order_acquire);
		while (r.inModel < counted) {
			if (source == nullptr) {
				r.inModel = counted; // the source went: its repeats keep their notes alone
				break;
			}
			append();
			r.inModel++;
		}
		whole = whole && r.inModel == r.appended.load(std::memory_order_acquire);
	};
	if (source != nullptr) {
		TakeStacks stacks{song, take, *source};
		drain(repeats, [&] {
			appendRepeat(repeats.inModel, repeats.paramLength, [&](int32_t offset, ManagerVisit visit) {
				take.forEachRepeatedManager(stacks.take, stacks.source, offset, visit);
			});
			repeats.paramLength += source->loopLength();
		});
		take.forEachRowWithOwnLength(
		    stacks.take, stacks.source,
		    [&](NoteRow& row, ModelStackWithNoteRow* thisRow, ModelStackWithNoteRow* otherRow) {
			    const int32_t otherLength = otherRow->getNoteRow()->loopLengthIfIndependent();
			    TakeRepeats& r = row.repeats;
			    drain(r, [&] {
				    appendRepeat(r.inModel, r.paramLength, rowWalk(row, thisRow, otherRow));
				    r.paramLength += otherLength;
			    });
		    });
	}
	else {
		drain(repeats, [] {});
		for (NoteRow& row : take.noteRows) {
			drain(row.repeats, [] {});
		}
	}
	if (take.beingRecordedFromClip == nullptr && whole) {
		repeats.source.store(nullptr, std::memory_order_relaxed); // the take is over and whole
	}
}

/// [render] At a loop end, the repeat numbered @p repeat of each manager @p walk visits at @p offset: appended into its
/// lanes' live working copies.
void appendRepeatIntoCopiesOf(uint32_t repeat, int32_t offset, ManagerWalk walk) {
	walk(offset, [&](ParamManagerForTimeline& mine, ParamManagerForTimeline& source, const RepeatArgs& args) {
		forEachAppendedCollection(
		    mine, source,
		    [&](ParamCollection& collection, ParamCollection& other, const ParamCollectionSummary& otherSummary) {
			    appendRepeatIntoCopies(collection, other, otherSummary, repeat, args);
		    });
	});
}

/// Whether the arrangement records @p take now, from a source still there.
bool recording(const InstrumentClip& take) {
	return playbackHandler.recording == RecordingMode::ARRANGEMENT && take.beingRecordedFromClip != nullptr
	       && take.repeats.source.load(std::memory_order_relaxed) != nullptr;
}

/// Stage the next of @p repeats on each collection @p walk's managers append to that does not hold it already: all of
/// them when what is staged was built for another repeat.
void stageRepeats(TakeRepeats& repeats, ManagerWalk walk) {
	const uint32_t next = repeats.appended.load(std::memory_order_acquire);
	const bool rebuild = repeats.stagedFor.load(std::memory_order_relaxed) != next;
	uint32_t collections = 0;
	bool all = true;
	repeats.anyStaged = true;
	walk(repeats.paramLength,
	     [&](ParamManagerForTimeline& mine, ParamManagerForTimeline& source, const RepeatArgs& args) {
		     forEachAppendedCollection(
		         mine, source,
		         [&](ParamCollection& collection, ParamCollection& other, const ParamCollectionSummary& otherSummary) {
			         collections++;
			         reserveRepeatInCopies(collection, other, otherSummary);
			         const uint32_t sourcePublications = other.publications();
			         if (!rebuild && LanePublication::stagedAgainst(collection, sourcePublications)) {
				         return;
			         }
			         bool staged = false;
			         try {
				         staged = LanePublication::stage(
				             collection,
				             [&](LaneVisit visit) {
					             collection.forEachLaneAppended(other, otherSummary, args.offset,
					                                            args.reverseThisRepeatWithLength,
					                                            args.pingpongingGenerally, visit);
				             },
				             sourcePublications);
			         } catch (...) {
				         // out of memory: the loop end counts the repeat as missed
			         }
			         all = all && staged;
		         });
	     });
	repeats.stagedCollections.store(collections, std::memory_order_relaxed);
	repeats.stagedFor.store(next, std::memory_order_relaxed);
	// A loop end while this built counted the repeat it is for: the next restage builds the one after
	repeats.staged.store(all && repeats.appended.load(std::memory_order_acquire) == next, std::memory_order_release);
}

/// [render] Publish the next of @p repeats: each collection @p walk's managers append to swaps in its staged snapshot,
/// or none does (counted).
bool swapRepeats(TakeRepeats& repeats, int32_t offset, ManagerWalk walk) {
	const uint32_t expected = repeats.stagedCollections.load(std::memory_order_relaxed);
	const bool staged = repeats.staged.exchange(false, std::memory_order_acq_rel);
	uint32_t found = 0;
	walk(offset, [&](ParamManagerForTimeline& mine, ParamManagerForTimeline&, const RepeatArgs&) {
		forEachCollection(mine,
		                  [&](ParamCollection& collection) { found += LanePublication::canSwap(collection) ? 1 : 0; });
	});
	// Staged for this repeat on every collection it appends to, or none is swapped
	if (!staged || found != expected
	    || repeats.stagedFor.load(std::memory_order_relaxed) != repeats.appended.load(std::memory_order_acquire)) {
#if ALPHA_OR_BETA_VERSION
		stagedRepeatMisses += expected != 0 ? 1 : 0;
#endif
		return false;
	}
	walk(offset, [&](ParamManagerForTimeline& mine, ParamManagerForTimeline&, const RepeatArgs&) {
		forEachCollection(mine, [&](ParamCollection& collection) {
			if (LanePublication::canSwap(collection)) {
				LanePublication::swap(collection);
				collection.ticksTilNextEvent = 0;
				mine.ticksTilNextEvent = 0;
			}
		});
	});
#if ALPHA_OR_BETA_VERSION
	stagedRepeatSwaps++;
#endif
	return true;
}

/// Stage @p take's next repeat, and each of its rows' with its own length, where caught up: one counted and not yet in
/// the model would be staged at the wrong length, and the next commit catches up.
void stage(Song& song, InstrumentClip& take) {
	InstrumentClip* source = take.repeats.source.load(std::memory_order_acquire);
	if (source == nullptr) {
		return; // the source went since the take was found recording
	}
	TakeStacks stacks{song, take, *source};
	take.repeats.anyStaged = true; // the take's own, or its rows'
	const auto caughtUp = [](const TakeRepeats& r) { return r.inModel == r.appended.load(std::memory_order_acquire); };
	if (caughtUp(take.repeats)) {
		stageRepeats(take.repeats, [&](int32_t offset, ManagerVisit visit) {
			take.forEachRepeatedManager(stacks.take, stacks.source, offset, visit);
		});
	}
	take.forEachRowWithOwnLength(stacks.take, stacks.source,
	                             [&](NoteRow& row, ModelStackWithNoteRow* thisRow, ModelStackWithNoteRow* otherRow) {
		                             if (caughtUp(row.repeats)) {
			                             stageRepeats(row.repeats, rowWalk(row, thisRow, otherRow));
		                             }
	                             });
}

/// Drop what is staged for @p take and its rows, and retire what their swaps replaced.
void drop(InstrumentClip& take) {
	bool settled = true;
	const auto settle = [&](ParamManagerForTimeline& manager, TakeRepeats& repeats) {
		forEachCollection(manager, [&](ParamCollection& collection) {
			settled = LanePublication::settleStaged(collection) && settled;
		});
		repeats.staged.store(false, std::memory_order_relaxed);
	};
	settle(take.paramManager, take.repeats);
	for (NoteRow& row : take.noteRows) {
		settle(row.paramManager, row.repeats);
	}
	take.repeats.anyStaged = !settled;
}
} // namespace

namespace {
/// Whether the arrangement records the audio take @p take now.
bool recording(const AudioClip& take) {
	return playbackHandler.recording == RecordingMode::ARRANGEMENT && take.beingRecordedFromClip != nullptr;
}

/// Delete @p take's spare, if any.
void dropAudioSpare(Song& song, AudioClip& take) {
	if (AudioClip* spare = take.spare.exchange(nullptr, std::memory_order_acq_rel)) {
		song.deleteClipObject(spare, false, InstrumentRemoval::NONE);
	}
}

/// Give @p take a spare, if it has none or one cloned from other source lanes: a clone of its source, its lanes
/// published, with room in its output's instances for the loop end to add the next take's. The room in the song's
/// arrangement-only clips is made for every audio take at once (restageArrangementTakes()).
void stageAudioSpare(Song& song, AudioClip& take) {
	// Read once: the render hands it over to the next take at a loop end, and lets go of it when the clip goes
	Clip* source = take.beingRecordedFromClip;
	if (source == nullptr) {
		return;
	}
	const ParamCollection* sourceParams = source->paramManager.summaries[0].paramCollection;
	const uint32_t against = sourceParams != nullptr ? sourceParams->publications() : 0;
	if (take.spare.load(std::memory_order_relaxed) != nullptr) {
		if (take.spareAgainst == against) {
			return;
		}
		dropAudioSpare(song, take); // cloned from the source's lanes as they were
	}
	if (!take.output->clipInstances.reserveExtra(1)) {
		return;
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* stack = setupModelStackWithSong(memory, &song)->addTimelineCounter(source);
	if (source->clone(stack) != Error::NONE) {
		return;
	}
	auto* spare = static_cast<AudioClip*>(stack->getTimelineCounter());
	if (!publishParamManager(spare->paramManager)) {
		song.deleteClipObject(spare, false, InstrumentRemoval::NONE);
		return;
	}
	take.spareAgainst = against;
	take.spare.store(spare, std::memory_order_release);
}

} // namespace

void noteAudioSpareMissed() {
#if ALPHA_OR_BETA_VERSION
	audioSpareMisses++;
#endif
}

void countRepeat(InstrumentClip& take) {
	take.repeats.appended.fetch_add(1, std::memory_order_release);
	g_repeatsPending.store(true, std::memory_order_release);
}

void countRowRepeat(NoteRow& row) {
	row.repeats.appended.fetch_add(1, std::memory_order_release);
	g_repeatsPending.store(true, std::memory_order_release);
}

void catchUpArrangementTakes(Song& song) {
	if (!g_repeatsPending.exchange(false, std::memory_order_acquire)) {
		return;
	}
	// By index, the size read again each time: the render inserts an audio take at the front at its loop end, which
	// at worst visits a take twice
	const ClipArray& takes = song.live.clips.arrangement_only_clips;
	for (size_t i = 0; i < takes.size(); i++) {
		if (takes[i]->type == ClipType::INSTRUMENT) {
			auto& take = static_cast<InstrumentClip&>(*takes[i]);
			if (take.repeats.source.load(std::memory_order_relaxed) != nullptr) {
				catchUp(song, take);
			}
		}
	}
}

void forgetRepeatSource(Song& song, const InstrumentClip& clip) {
	for (Clip* other : song.live.clips.arrangement_only_clips) {
		if (other->type == ClipType::INSTRUMENT) {
			InstrumentClip* expected = const_cast<InstrumentClip*>(&clip);
			(void)static_cast<InstrumentClip*>(other)->repeats.source.compare_exchange_strong(
			    expected, nullptr, std::memory_order_relaxed);
		}
	}
}

void restageArrangementTakes(Song& song) {
	ClipArray& takes = song.live.clips.arrangement_only_clips;
	// Room for every audio take's next take at once: their loop ends may fall in one block
	int32_t audioTakes = 0;
	for (size_t i = 0; i < takes.size(); i++) {
		audioTakes += takes[i]->type == ClipType::AUDIO && recording(static_cast<AudioClip&>(*takes[i])) ? 1 : 0;
	}
	if (audioTakes != 0) {
		(void)takes.reserveExtra(audioTakes);
	}
	// By index: the render inserts into this container at an audio take's loop end
	for (size_t i = 0; i < takes.size(); i++) {
		Clip* clip = takes[i];
		if (clip->type == ClipType::AUDIO) {
			auto& take = static_cast<AudioClip&>(*clip);
			if (recording(take)) {
				stageAudioSpare(song, take);
			}
			else {
				dropAudioSpare(song, take);
			}
			continue;
		}
		if (clip->type != ClipType::INSTRUMENT) {
			continue;
		}
		auto& take = static_cast<InstrumentClip&>(*clip);
		if (recording(take)) {
			stage(song, take);
		}
		else if (take.repeats.anyStaged) {
			drop(take);
		}
	}
}

void appendRepeatIntoTakeCopies(InstrumentClip& take, ModelStackWithTimelineCounter* thisModelStack,
                                ModelStackWithTimelineCounter* otherModelStack) {
	const int32_t offset = take.loopLength() - ((Clip*)otherModelStack->getTimelineCounter())->loopLength();
	appendRepeatIntoCopiesOf(take.repeats.appended.load(std::memory_order_relaxed), offset,
	                         [&](int32_t at, ManagerVisit visit) {
		                         take.forEachRepeatedManager(thisModelStack, otherModelStack, at, visit);
	                         });
}

void appendRepeatIntoRowCopies(NoteRow& row, ModelStackWithNoteRow* rowModelStack,
                               ModelStackWithNoteRow* otherRowModelStack) {
	const int32_t offset = row.loopLengthIfIndependent() - otherRowModelStack->getNoteRow()->loopLengthIfIndependent();
	appendRepeatIntoCopiesOf(row.repeats.appended.load(std::memory_order_relaxed), offset,
	                         rowWalk(row, rowModelStack, otherRowModelStack));
}

const TakeRepeats* takeRepeatsOf(const ModelStackWithAutoParam& lane) {
	TimelineCounter* counter = lane.getTimelineCounterAllowNull();
	if (counter == nullptr || counter == lane.song) {
		return nullptr; // a lane of the song's own params
	}
	auto* clip = static_cast<Clip*>(counter);
	if (clip->type != ClipType::INSTRUMENT || !clip->isArrangementOnlyClip()) {
		return nullptr;
	}
	auto& take = static_cast<InstrumentClip&>(*clip);
	if (take.repeats.source.load(std::memory_order_relaxed) == nullptr) {
		return nullptr;
	}
	NoteRow* row = lane.getNoteRowAllowNull();
	return row != nullptr && row->loopLengthIfIndependent() != 0 ? &row->repeats : &take.repeats;
}

bool swapStagedRepeat(InstrumentClip& take, ModelStackWithTimelineCounter* thisModelStack,
                      ModelStackWithTimelineCounter* otherModelStack) {
	const int32_t offset = take.loopLength() - ((Clip*)otherModelStack->getTimelineCounter())->loopLength();
	return swapRepeats(take.repeats, offset, [&](int32_t at, ManagerVisit visit) {
		take.forEachRepeatedManager(thisModelStack, otherModelStack, at, visit);
	});
}

bool swapStagedRowRepeat(NoteRow& row, ModelStackWithNoteRow* rowModelStack,
                         ModelStackWithNoteRow* otherRowModelStack) {
	const int32_t offset = row.loopLengthIfIndependent() - otherRowModelStack->getNoteRow()->loopLengthIfIndependent();
	return swapRepeats(row.repeats, offset, rowWalk(row, rowModelStack, otherRowModelStack));
}

#if defined(DELUGE_HOST)
namespace {
void (*g_takeLoopEndHook)(InstrumentClip&, NoteRow*) = nullptr;
} // namespace

void setTakeLoopEndHook(void (*hook)(InstrumentClip& take, NoteRow* row)) {
	g_takeLoopEndHook = hook;
}

void takeLoopEnded(InstrumentClip& take, NoteRow* row) {
	if (g_takeLoopEndHook != nullptr) {
		g_takeLoopEndHook(take, row);
	}
}
#endif

} // namespace deluge::modulation
