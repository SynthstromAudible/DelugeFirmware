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
#pragma once

#include <cstdint>

class Clip;
class Song;

/// The render's hand-over of interaction-owned work.
///
/// Playback runs on the render, and some of what it does changes state only the interaction tier owns: the view's
/// active model stack, MIDI follow's stored clips, the UI mode, the display. The render never writes that state. It
/// posts a request, and the interaction tier does the work before its next job. Called on the interaction tier, the
/// same entry points do the work at once, so an immediate and an armed song swap run one code path.
namespace deluge::playback {

/// @brief [render] or [interaction] A song swap has taken @p outgoing out of play and made its successor current: do
///        the interaction tier's side of the swap.
///
/// That side is: stop the outgoing song's auditioning, let go of the view's active model stack, forget MIDI follow's
/// stored clips from the outgoing song, tell the instrument load a song was made current, and show the load's LOADING
/// mode and animation. On the interaction tier it is done at once. On the render it is posted, and done before the
/// interaction tier's next job: until then an interaction handler the render preempted sees everything still naming @p
/// outgoing, which stays alive until the interaction tier hands it to AudioEngine::retire(Song*) -- which applies the
/// requests first.
/// @param outgoing the song swapped out, or nullptr when there was none (it was already retired)
void songSwappedOut(Song* outgoing);

/// @brief [render] or [interaction] A repeat of the armed launch has passed and more remain: show how many
///        (Session::showLaunchRepeatsRemaining()) -- at once on the interaction tier, by request on the render.
void launchRepeatCounted();

/// @brief [interaction] Do, in order, everything the render has asked for since the last call.
///
/// Called before every app-loop job (deluge_app_service()), by AudioEngine::retire(Clip*) and retire(Song*) before
/// they hand anything over (requests name only songs and clips), so no request is left naming an object once the
/// interaction tier has let go of it, and by each entry point above on the interaction tier before its own work.
/// Interaction tier only: it is the queue's one consumer.
void applyRenderRequests();

/// Interaction-owned state the render must not write, for the host harness's watch.
enum class InteractionOwned : uint8_t {
	MidiFollowStoredClips,  ///< MidiFollow's clipForLastNoteReceived
	LoadingAnimation,       ///< the display's working / loading animation
	InstrumentLoadNotified, ///< InstrumentLoad::songMadeCurrent()
	UiMode,                 ///< currentUIMode
	LoadSongFlag,           ///< LoadSongUI::deletedPartsOfOldSong
	Auditioning,            ///< Song::stopAllAuditioning()
	kCount,
};

#if defined(DELUGE_HOST)
/// @brief Host only: count a write to @p what, and count it as a render write when made on the audio tier while
///        watched. Called from the writers that are functions; the plain globals are watched per render block.
void noteInteractionOwnedWrite(InteractionOwned what);
/// @brief Host only: start or stop counting render writes to interaction-owned state, and reset the counts.
void watchRenderInteractionWrites(bool on);
/// @brief Host only: writes to @p what made by the render while watched.
int32_t renderInteractionWrites(InteractionOwned what);
/// @brief Host only: writes to @p what on any tier, watched or not.
int32_t interactionOwnedWrites(InteractionOwned what);
/// @brief Host only: fill the request queue at the render's next song swap, so the swap's place in it is lost and
///        only its own slot carries it.
void floodRenderRequestsAtNextSwap();
/// @brief Host only: how many of the render's requests found the queue full.
int32_t renderRequestOverflows();
/// @brief Host only: whether the outgoing song of the last swap whose interaction side ran still had an output
///        auditioning after it (1) or not (0), or -1 if no swap's side has run since the last call.
int32_t outgoingAuditioningAfterLastSwap();
/// @brief Host only: put around a render block; counts it as a render write to each plain global it changed.
///
/// Reads currentUIMode and LoadSongUI::deletedPartsOfOldSong, plain globals the interaction tier writes, on the render:
/// a harness probe for the single-executor golden_vt_render, not for use under the TSan lens, where it would itself be
/// a race.
class RenderBlockInteractionWatch {
public:
	RenderBlockInteractionWatch();
	~RenderBlockInteractionWatch();
	RenderBlockInteractionWatch(const RenderBlockInteractionWatch&) = delete;
	RenderBlockInteractionWatch& operator=(const RenderBlockInteractionWatch&) = delete;

private:
	bool watching_;
	uint32_t uiMode_ = 0;
	bool loadSongFlag_ = false;
};
#else
inline void noteInteractionOwnedWrite(InteractionOwned) {
}
#endif

} // namespace deluge::playback
