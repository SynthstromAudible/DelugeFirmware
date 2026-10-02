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

// View::activeModControllableModelStack's hand-over: only the interaction tier writes the stack, and the render asks
// it for changes through the queue below.

#include "gui/views/view.h"
#include "libdeluge/tier.h"
#include "model/clip/clip.h"
#include "model/clip/clip_array.h"
#include "model/model_stack.h"
#include "model/output.h"
#include "model/song/song.h"
#include "util/spsc_ring.h"

#include <atomic>
#include <cstdint>

bool View::activeModControllableNamesSong(Song* song) const {
	ModelStackWithThreeMainThings const& stack = activeModControllableModelStack;
	TimelineCounter const* counter = stack.getTimelineCounterAllowNull();
	if (counter == song || stack.modControllable == &song->globalEffectable) {
		return true;
	}
	for (ClipArray const* array : {&song->sessionClips, &song->arrangementOnlyClips}) {
		for (Clip const* clip : *array) {
			if (counter == clip) {
				return true;
			}
		}
	}
	if (stack.modControllable != nullptr) {
		for (Output* first : {song->firstOutput, static_cast<Output*>(song->firstHibernatingInstrument)}) {
			for (Output* output = first; output != nullptr; output = output->next) {
				if (stack.modControllable == output->toModControllable()) {
					return true;
				}
			}
		}
	}
	return false;
}

namespace {
/// A change to View::activeModControllableModelStack the render asks the interaction tier to make.
struct StackRequest {
	enum class Kind : uint8_t { SONG_SWAPPED_OUT, RECORDING_MOVED };
	Kind kind = Kind::SONG_SWAPPED_OUT;
	/// SONG_SWAPPED_OUT: the outgoing song.
	Song* song = nullptr;
	/// RECORDING_MOVED: the clip the recording left, and the clone it moved on to.
	Clip* from = nullptr;
	Clip* to = nullptr;
};

/// [render] -> [interaction] The render's requests, in order. A song swap posts one, and a recording into the arranger
/// one per clip whose loop ends in a block, so 16 is far more than a stretch between two jobs fills.
deluge::util::SpscRing<StackRequest, 16> g_stackRequests;
/// [render] -> [interaction] Set when a request found the queue full and was dropped: the interaction tier then
/// releases the stack, since it can no longer tell what the dropped request would have left it naming.
std::atomic<bool> g_stackRequestsOverflowed{false};

/// [interaction] Make the change @p request asks for, if it still applies.
void applyStackRequest(View& v, StackRequest const& request) {
	ModelStackWithThreeMainThings& stack = v.activeModControllableModelStack;
	switch (request.kind) {
	case StackRequest::Kind::SONG_SWAPPED_OUT:
		if (request.song != nullptr && v.activeModControllableNamesSong(request.song)) {
			v.releaseActiveModControllable();
		}
		break;
	case StackRequest::Kind::RECORDING_MOVED:
		// The clone records on the same output, so the ModControllable stays.
		if (stack.getTimelineCounterAllowNull() == request.from) {
			stack.setTimelineCounter(request.to);
			stack.paramManager = &request.to->paramManager;
		}
		break;
	}
}

/// [render] or [interaction] On the render, queue @p request for View::applyPostedStackRequests(), or if the queue is
/// full ask for a release instead. The queue has one producer, the render; on the interaction tier the change is made
/// at once.
void postStackRequest(View& v, StackRequest const& request) {
	if (deluge_tier_current() != DELUGE_TIER_AUDIO) {
		applyStackRequest(v, request);
		return;
	}
	if (!g_stackRequests.try_push(request)) {
		g_stackRequestsOverflowed.store(true, std::memory_order_release);
	}
}
} // namespace

void View::songSwappedOut(Song* outgoing) {
	if (deluge_tier_current() != DELUGE_TIER_AUDIO) {
		releaseActiveModControllable();
		return;
	}
	postStackRequest(*this, {.kind = StackRequest::Kind::SONG_SWAPPED_OUT, .song = outgoing});
}

void View::recordingMovedToClip(Clip* from, Clip* to) {
	postStackRequest(*this, {.kind = StackRequest::Kind::RECORDING_MOVED, .from = from, .to = to});
}

void View::applyPostedStackRequests() {
	// Every object a request names is still alive here: a swapped-out song is freed only after
	// AudioEngine::retire(Song*), and a clip only after AudioEngine::retire(Clip*), both of which apply the requests
	// first.
	(void)g_stackRequests.drain([this](StackRequest&& request) { applyStackRequest(*this, request); },
	                            g_stackRequests.capacity());
	if (g_stackRequestsOverflowed.exchange(false, std::memory_order_acq_rel)) {
		releaseActiveModControllable();
	}
}
