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

#include <atomic>

namespace deluge::modulation {

namespace {
/// [render] or [interaction] set, [interaction] cleared: a repeat was counted since the last catch-up.
std::atomic<bool> g_repeatsPending{false};

/// Append the automation of @p take's repeats counted and not yet in the model.
void catchUp(Song& song, InstrumentClip& take) {
	const uint32_t counted = take.repeatsAppended.load(std::memory_order_acquire);
	InstrumentClip* source = take.repeatSource.load(std::memory_order_acquire);
	while (take.repeatsInModel < counted) {
		if (source == nullptr) {
			take.repeatsInModel = counted; // the source went: its repeats keep their notes alone
			break;
		}
		alignas(ModelStackWithAutoParam) char thisMemory[MODEL_STACK_MAX_SIZE];
		alignas(ModelStackWithAutoParam) char otherMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* thisStack =
		    setupModelStackWithSong(thisMemory, &song)->addTimelineCounter(&take);
		ModelStackWithTimelineCounter* otherStack =
		    setupModelStackWithSong(otherMemory, &song)->addTimelineCounter(source);
		take.appendRepeatParams(thisStack, otherStack, take.paramLength);
		take.paramLength += source->loopLength();
		take.repeatsInModel++;
	}
	if (take.beingRecordedFromClip == nullptr && take.repeatsInModel == counted) {
		take.repeatSource.store(nullptr, std::memory_order_relaxed); // the take is over and whole
	}
}
} // namespace

void countRepeat(InstrumentClip& take) {
	take.repeatsAppended.fetch_add(1, std::memory_order_release);
	g_repeatsPending.store(true, std::memory_order_release);
}

void catchUpArrangementTakes(Song& song) {
	if (!g_repeatsPending.exchange(false, std::memory_order_acquire)) {
		return;
	}
	for (Clip* clip : song.live.clips.arrangement_only_clips) {
		if (clip->type == ClipType::INSTRUMENT) {
			auto& take = static_cast<InstrumentClip&>(*clip);
			if (take.repeatSource.load(std::memory_order_relaxed) != nullptr) {
				catchUp(song, take);
			}
		}
	}
}

void forgetRepeatSource(Song& song, const InstrumentClip& clip) {
	for (Clip* other : song.live.clips.arrangement_only_clips) {
		if (other->type == ClipType::INSTRUMENT) {
			auto& take = static_cast<InstrumentClip&>(*other);
			InstrumentClip* expected = const_cast<InstrumentClip*>(&clip);
			(void)take.repeatSource.compare_exchange_strong(expected, nullptr, std::memory_order_relaxed);
		}
	}
}

} // namespace deluge::modulation
