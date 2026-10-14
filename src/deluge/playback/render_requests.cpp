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
#include "model/instrument/instrument.h"
#include "model/output.h"
#include "model/song/song.h"
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
	/// NOTHING: host only, a filler the harness floods the queue with.
	enum class Kind : uint8_t { SONG_SWAPPED_OUT, RECORDING_MOVED, LAUNCH_REPEAT_COUNTED, NOTHING };
	Kind kind = Kind::SONG_SWAPPED_OUT;
	/// Made on the render, so applied after other interaction work may have run since: changes only what still names
	/// the objects the request names. Made on the interaction tier, it is applied at the swap itself.
	bool posted = false;
	/// SONG_SWAPPED_OUT made on the interaction tier: the outgoing song. Posted, the request is only a marker of where
	/// the swap falls among the others; the outgoing song travels in g_pendingSwap.
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
/// stack and forgets every MIDI follow stored clip not of the current song. A song swap is never lost this way: see
/// g_pendingSwap.
std::atomic<bool> g_requestsOverflowed{false};

/// [render] -> [interaction] A swap the render made whose interaction side has not run yet, in a slot of its own so it
/// can never be dropped: the outgoing song's auditioning must be stopped (an external instrument's note-off) whatever
/// else the queue lost. One slot is enough: another swap needs another load, and a load reaches its next swap only
/// through the interaction tier's runs, each of which applies this one first.
struct PendingSwap {
	/// Written by the render before `pending` is set, read by the interaction tier after it is taken.
	Song* outgoing = nullptr;
	std::atomic<bool> pending{false};
};
PendingSwap g_pendingSwap;

#if defined(DELUGE_HOST)
/// Host only: fill the queue at the render's next swap, so the swap's marker overflows it.
std::atomic<bool> g_floodNextSwap{false};
std::atomic<int32_t> g_overflows{0};
/// Host only: whether the outgoing song of the last swap whose interaction side ran was still auditioning after it.
std::atomic<int32_t> g_outgoingAuditioning{-1};

/// [render] Host only: if asked, fill the queue with requests that do nothing.
void floodIfAsked() {
	if (g_floodNextSwap.exchange(false, std::memory_order_acq_rel)) {
		while (g_requests.try_push({.kind = RenderRequest::Kind::NOTHING, .posted = true})) {}
	}
}

/// [interaction] Host only: whether any output of @p song is auditioning.
bool anyAuditioning(Song* song) {
	for (Output* output = song->live.media.first_output; output != nullptr; output = output->next) {
		const bool instrument = output->type == OutputType::SYNTH || output->type == OutputType::KIT
		                        || output->type == OutputType::MIDI_OUT || output->type == OutputType::CV;
		if (instrument && static_cast<Instrument*>(output)->isAnyAuditioningHappening()) {
			return true;
		}
	}
	return false;
}
#endif

/// [interaction] The interaction tier's side of a song swap. currentSong is already the incoming song.
void songSwapInteractionSide(Song* outgoing, bool posted) {
	// Auditioning is the interaction tier's: input and MIDI handlers start and end it. The outgoing song is alive until
	// its retire, which applies the requests first.
	if (outgoing != nullptr) {
		outgoing->stopAllAuditioning();
		// Its stutter ends and gives the tape back here, on the interaction tier, before the retire frees the song; the
		// UI mode set below leaves the stutter's mode.
		outgoing->live.stutter.end();
#if defined(DELUGE_HOST)
		g_outgoingAuditioning.store(anyAuditioning(outgoing) ? 1 : 0, std::memory_order_relaxed);
#endif
	}
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
	// Shown directly, not queued as a notice: the swap's interaction side is the first interaction-tier entry after
	// the swap, and the animation must already be up for whatever job runs next.
	display->displayLoadingAnimationText("Loading");
	currentUIMode = UI_MODE_LOADING_SONG_NEW_SONG_PLAYING;
}

/// [interaction] Do the interaction side of the render's swap, if one is waiting.
void applyPendingSwap() {
	if (g_pendingSwap.pending.exchange(false, std::memory_order_acquire)) {
		songSwapInteractionSide(g_pendingSwap.outgoing, true);
	}
}

/// [interaction] Do what @p request asks for.
void applyRequest(RenderRequest const& request) {
	switch (request.kind) {
	case RenderRequest::Kind::SONG_SWAPPED_OUT:
		if (request.posted) {
			applyPendingSwap();
		}
		else {
			songSwapInteractionSide(request.song, false);
		}
		break;
	case RenderRequest::Kind::RECORDING_MOVED:
		view.followRecordingToClip(request.from, request.to);
		break;
	case RenderRequest::Kind::LAUNCH_REPEAT_COUNTED:
		session.showLaunchRepeatsRemaining();
		break;
	case RenderRequest::Kind::NOTHING:
		break;
	}
}

/// [render] Queue @p request for applyRenderRequests(), or if the queue is full note what was dropped. The render is
/// the queue's one producer.
void postRequest(RenderRequest request) {
	request.posted = true;
	if (request.kind == RenderRequest::Kind::SONG_SWAPPED_OUT) {
		g_pendingSwap.outgoing = request.song;
		g_pendingSwap.pending.store(true, std::memory_order_release);
		request.song = nullptr;
#if defined(DELUGE_HOST)
		floodIfAsked();
#endif
	}
	if (!g_requests.try_push(request)) {
		g_requestsOverflowed.store(true, std::memory_order_release);
#if defined(DELUGE_HOST)
		g_overflows.fetch_add(1, std::memory_order_relaxed);
#endif
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
	// A swap whose marker the full queue dropped.
	applyPendingSwap();
	if (g_requestsOverflowed.exchange(false, std::memory_order_acq_rel)) {
		view.releaseActiveModControllable();
		midiFollow.forgetClipsNotOfCurrentSong();
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

void floodRenderRequestsAtNextSwap() {
	g_floodNextSwap.store(true, std::memory_order_release);
}

int32_t renderRequestOverflows() {
	return g_overflows.load(std::memory_order_acquire);
}

int32_t outgoingAuditioningAfterLastSwap() {
	return g_outgoingAuditioning.exchange(-1, std::memory_order_acq_rel);
}

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
