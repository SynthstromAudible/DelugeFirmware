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

#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"
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
/// summary: as appendParamManager() pairs them.
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

/// Append the automation of @p take's repeats counted and not yet in the model.
void catchUp(Song& song, InstrumentClip& take) {
	TakeRepeats& repeats = take.repeats;
	const uint32_t counted = repeats.appended.load(std::memory_order_acquire);
	InstrumentClip* source = repeats.source.load(std::memory_order_acquire);
	while (repeats.inModel < counted) {
		if (source == nullptr) {
			repeats.inModel = counted; // the source went: its repeats keep their notes alone
			break;
		}
		TakeStacks stacks{song, take, *source};
		take.appendRepeatParams(stacks.take, stacks.source, repeats.paramLength);
		repeats.paramLength += source->loopLength();
		repeats.inModel++;
	}
	if (take.beingRecordedFromClip == nullptr && repeats.inModel == counted) {
		repeats.source.store(nullptr, std::memory_order_relaxed); // the take is over and whole
	}
}

/// Whether the arrangement records @p take now, from a source still there.
bool recording(const InstrumentClip& take) {
	return playbackHandler.recording == RecordingMode::ARRANGEMENT && take.beingRecordedFromClip != nullptr
	       && take.repeats.source.load(std::memory_order_relaxed) != nullptr;
}

/// Stage @p take's next repeat on each collection it appends to that does not hold it already.
void stage(Song& song, InstrumentClip& take) {
	TakeRepeats& repeats = take.repeats;
	TakeStacks stacks{song, take, *repeats.source.load(std::memory_order_relaxed)};
	uint32_t collections = 0;
	bool all = true;
	repeats.anyStaged = true;
	take.forEachRepeatedManager(
	    stacks.take, stacks.source, repeats.paramLength,
	    [&](ParamManagerForTimeline& mine, ParamManagerForTimeline& source, const RepeatArgs& args) {
		    forEachAppendedCollection(
		        mine, source,
		        [&](ParamCollection& collection, ParamCollection& other, const ParamCollectionSummary& otherSummary) {
			        collections++;
			        if (LanePublication::stagedAgainst(collection, other.published())) {
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
				            other.published());
			        } catch (...) {
				        // out of memory: the loop end counts the repeat as missed
			        }
			        all = all && staged;
		        });
	    });
	repeats.stagedCollections.store(collections, std::memory_order_relaxed);
	repeats.staged.store(all, std::memory_order_release);
}

/// Drop what is staged for @p take, and retire what its swaps replaced.
void drop(Song& song, InstrumentClip& take) {
	bool settled = true;
	const auto settle = [&](ParamManagerForTimeline& manager) {
		forEachCollection(manager, [&](ParamCollection& collection) {
			settled = LanePublication::settleStaged(collection) && settled;
		});
	};
	settle(take.paramManager);
	for (NoteRow& row : take.noteRows) {
		settle(row.paramManager);
	}
	take.repeats.staged.store(false, std::memory_order_relaxed);
	take.repeats.anyStaged = !settled;
}
} // namespace

void countRepeat(InstrumentClip& take) {
	take.repeats.appended.fetch_add(1, std::memory_order_release);
	g_repeatsPending.store(true, std::memory_order_release);
}

void catchUpArrangementTakes(Song& song) {
	if (!g_repeatsPending.exchange(false, std::memory_order_acquire)) {
		return;
	}
	for (Clip* clip : song.live.clips.arrangement_only_clips) {
		if (clip->type == ClipType::INSTRUMENT) {
			auto& take = static_cast<InstrumentClip&>(*clip);
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
	for (Clip* clip : song.live.clips.arrangement_only_clips) {
		if (clip->type != ClipType::INSTRUMENT) {
			continue;
		}
		auto& take = static_cast<InstrumentClip&>(*clip);
		TakeRepeats& repeats = take.repeats;
		if (!recording(take)) {
			if (repeats.anyStaged) {
				drop(song, take);
			}
			continue;
		}
		// A repeat counted and not yet in the model would be staged at the wrong length: the next commit catches up
		if (repeats.inModel == repeats.appended.load(std::memory_order_acquire)) {
			stage(song, take);
		}
	}
}

bool swapStagedRepeat(InstrumentClip& take, ModelStackWithTimelineCounter* thisModelStack,
                      ModelStackWithTimelineCounter* otherModelStack) {
	TakeRepeats& repeats = take.repeats;
	const uint32_t expected = repeats.stagedCollections.load(std::memory_order_relaxed);
	const bool staged = repeats.staged.exchange(false, std::memory_order_acq_rel);
	uint32_t found = 0;
	const auto offset = take.loopLength();
	take.forEachRepeatedManager(thisModelStack, otherModelStack, offset,
	                            [&](ParamManagerForTimeline& mine, ParamManagerForTimeline&, const RepeatArgs&) {
		                            forEachCollection(mine, [&](ParamCollection& collection) {
			                            found += LanePublication::canSwap(collection) ? 1 : 0;
		                            });
	                            });
	if (!staged || found != expected) {
#if ALPHA_OR_BETA_VERSION
		stagedRepeatMisses += expected != 0 ? 1 : 0;
#endif
		return false;
	}
	take.forEachRepeatedManager(thisModelStack, otherModelStack, offset,
	                            [&](ParamManagerForTimeline& mine, ParamManagerForTimeline&, const RepeatArgs&) {
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

#if defined(DELUGE_HOST)
namespace {
void (*g_takeLoopEndHook)(InstrumentClip&) = nullptr;
} // namespace

void setTakeLoopEndHook(void (*hook)(InstrumentClip& take)) {
	g_takeLoopEndHook = hook;
}

void takeLoopEnded(InstrumentClip& take) {
	if (g_takeLoopEndHook != nullptr) {
		g_takeLoopEndHook(take);
	}
}
#endif

} // namespace deluge::modulation
