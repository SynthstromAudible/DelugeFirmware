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

#include "playback/render_requests.h"

#include "definitions_cxx.hpp"
#include "foundation/panic.h"
#include "gui/ui/load/instrument_load.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "io/midi/midi_follow.h"
#include "libdeluge/tier.h"
#include "playback/mode/session.h"
#include "util/spsc_ring.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace deluge::playback {
namespace {
/// Work on interaction-owned state the render asks the interaction tier to do.
struct RenderRequest {
	enum class Kind : uint8_t { SONG_SWAPPED_OUT, RECORDING_MOVED, LAUNCH_REPEAT_COUNTED };
	Kind kind = Kind::SONG_SWAPPED_OUT;
	/// Made on the render, so applied after other interaction work may have run since: changes only what still names
	/// the objects the request names. Made on the interaction tier, it is applied at the swap itself.
	bool posted = false;
	/// SONG_SWAPPED_OUT: the outgoing song.
	Song* song = nullptr;
	/// RECORDING_MOVED: the clip the recording left, and the clone it moved on to.
	Clip* from = nullptr;
	Clip* to = nullptr;
};

/// [render] -> [interaction] The render's requests, in order. A song swap posts one, an armed launch one per repeat
/// counted, and a recording into the arranger one per clip whose loop ends in a block. A job can span many blocks (an
/// AudioEngine::awaitRetired() wait, a card wait), so the ring can fill; see g_requestsOverflowed for what a dropped
/// request becomes.
deluge::util::SpscRing<RenderRequest, 16> g_requests;
/// [render] -> [interaction] Set when a request found the queue full and was dropped. The interaction tier can no
/// longer tell what it named, so it does the conservative part of every request: it releases the view's active model
/// stack and forgets every MIDI follow stored clip not of the current song.
std::atomic<bool> g_requestsOverflowed{false};
/// [render] -> [interaction] Set before g_requestsOverflowed when a dropped request was a song swap: the interaction
/// tier then does all of that swap's side, the knobs let go of whatever they name.
std::atomic<bool> g_swapDropped{false};

/// [interaction] The interaction tier's side of a song swap. currentSong is already the incoming song.
void songSwapInteractionSide(Song* outgoing, bool posted) {
	// Posted, the knobs are let go of only if they still name the outgoing song: an interaction job the render
	// preempted may have put them on the incoming one since. At the swap itself, nothing else can be under them.
	if (!posted || (outgoing != nullptr && view.activeModControllableNamesSong(outgoing))) {
		view.releaseActiveModControllable();
	}
	// Not cleared outright: a note-on a preempted MIDI handler sent to the incoming song keeps its clip, so its
	// note-off still reaches it.
	midiFollow.forgetClipsNotOfCurrentSong();
	loadSongUI.deletedPartsOfOldSong = false;
	// A song just made current may be a blank one asking for its first preset.
	deluge::gui::instrumentLoad.songMadeCurrent();
	// More loading might need to happen now, or still be happening. The mode is also what moves a load waiting on an
	// armed swap on to its finish (LoadSongUI's AWAIT_ARM phase).
	display->displayLoadingAnimationText("Loading");
	currentUIMode = UI_MODE_LOADING_SONG_NEW_SONG_PLAYING;
}

/// [interaction] Do what @p request asks for.
void applyRequest(RenderRequest const& request) {
	switch (request.kind) {
	case RenderRequest::Kind::SONG_SWAPPED_OUT:
		songSwapInteractionSide(request.song, request.posted);
		break;
	case RenderRequest::Kind::RECORDING_MOVED:
		view.followRecordingToClip(request.from, request.to);
		break;
	case RenderRequest::Kind::LAUNCH_REPEAT_COUNTED:
		session.showLaunchRepeatsRemaining();
		break;
	}
}

/// [render] Queue @p request for applyRenderRequests(), or if the queue is full note what was dropped. The render is
/// the queue's one producer.
void postRequest(RenderRequest request) {
	request.posted = true;
	if (!g_requests.try_push(request)) {
		if (request.kind == RenderRequest::Kind::SONG_SWAPPED_OUT) {
			g_swapDropped.store(true, std::memory_order_relaxed);
		}
		g_requestsOverflowed.store(true, std::memory_order_release);
	}
}

/// The interaction-owned state has one writer, the interaction tier, and the queue one producer, the render: any other
/// tier here is a call made from the wrong place.
void wrongTier() {
	FREEZE_WITH_ERROR("E464");
}

/// [render] or [interaction] Apply @p request at once on the interaction tier, after what the render asked for first;
/// post it on the render.
void request(RenderRequest const& request) {
	switch (deluge_tier_current()) {
	case DELUGE_TIER_INTERACTION:
		applyRenderRequests();
		applyRequest(request);
		return;
	case DELUGE_TIER_AUDIO:
		postRequest(request);
		return;
	default:
		wrongTier();
	}
}
} // namespace

void songSwappedOut(Song* outgoing) {
	request({.kind = RenderRequest::Kind::SONG_SWAPPED_OUT, .song = outgoing});
}

void recordingMovedToClip(Clip* from, Clip* to) {
	request({.kind = RenderRequest::Kind::RECORDING_MOVED, .from = from, .to = to});
}

void launchRepeatCounted() {
	request({.kind = RenderRequest::Kind::LAUNCH_REPEAT_COUNTED});
}

void applyRenderRequests() {
	// The queue's one consumer.
	if (deluge_tier_current() != DELUGE_TIER_INTERACTION) {
		wrongTier();
		return;
	}
	// Every object a request names is still alive here: a swapped-out song is freed only after
	// AudioEngine::retire(Song*), and a clip only after AudioEngine::retire(Clip*), both of which apply the requests
	// first.
	(void)g_requests.drain([](RenderRequest&& request) { applyRequest(request); }, g_requests.capacity());
	if (g_requestsOverflowed.exchange(false, std::memory_order_acq_rel)) {
		if (g_swapDropped.exchange(false, std::memory_order_relaxed)) {
			songSwapInteractionSide(nullptr, false);
		}
		else {
			view.releaseActiveModControllable();
			midiFollow.forgetClipsNotOfCurrentSong();
		}
	}
}

#if defined(DELUGE_HOST)
namespace {
constexpr size_t kOwnedCount = static_cast<size_t>(InteractionOwned::kCount);
std::atomic<bool> g_watching{false};
std::array<std::atomic<int32_t>, kOwnedCount> g_renderWrites{};
std::array<std::atomic<int32_t>, kOwnedCount> g_writes{};

void countRenderWrite(InteractionOwned what) {
	g_renderWrites[static_cast<size_t>(what)].fetch_add(1, std::memory_order_relaxed);
}
} // namespace

void noteInteractionOwnedWrite(InteractionOwned what) {
	g_writes[static_cast<size_t>(what)].fetch_add(1, std::memory_order_relaxed);
	if (g_watching.load(std::memory_order_acquire) && deluge_tier_current() == DELUGE_TIER_AUDIO) {
		countRenderWrite(what);
	}
}

void watchRenderInteractionWrites(bool on) {
	for (auto& count : g_renderWrites) {
		count.store(0, std::memory_order_relaxed);
	}
	g_watching.store(on, std::memory_order_release);
}

int32_t renderInteractionWrites(InteractionOwned what) {
	return g_renderWrites[static_cast<size_t>(what)].load(std::memory_order_acquire);
}

int32_t interactionOwnedWrites(InteractionOwned what) {
	return g_writes[static_cast<size_t>(what)].load(std::memory_order_acquire);
}

RenderBlockInteractionWatch::RenderBlockInteractionWatch() : watching_{g_watching.load(std::memory_order_acquire)} {
	if (watching_) {
		uiMode_ = currentUIMode;
		loadSongFlag_ = loadSongUI.deletedPartsOfOldSong;
	}
}

RenderBlockInteractionWatch::~RenderBlockInteractionWatch() {
	if (!watching_) {
		return;
	}
	if (currentUIMode != uiMode_) {
		countRenderWrite(InteractionOwned::UiMode);
	}
	if (loadSongUI.deletedPartsOfOldSong != loadSongFlag_) {
		countRenderWrite(InteractionOwned::LoadSongFlag);
	}
}
#endif

} // namespace deluge::playback
