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

/// @file
/// @brief The SD card's own lifecycle, over the C ABI: what every loader has to forget when the
///        card is pulled.
///
/// One entry point. It is card-scoped rather than loader-scoped deliberately: an ejection
/// invalidates everything read from that card at once, so the BSP keeps a single card "generation"
/// that every loader caching card-derived state stamps its work with, and this is the one call that
/// advances it. A second loader gets covered by calling this, not by growing a second flush.
#ifndef LIBDELUGE_CARD_H
#define LIBDELUGE_CARD_H

#include "abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Discard everything every loader read from the card that has just been removed. Never
///        waits.
///
/// Call this when the card is EJECTED. A loader's cache is keyed by the nominal path and the path
/// outlives the card it was read from, so a result parsed from one card must never be applied after
/// another has been inserted: for the audio loader (@ref audio_load.h), the app's sample build would
/// open the NEW card's file at that path and write the OLD card's geometry onto it. That loads with no
/// error and plays the wrong thing -- audio_load.h's own "worst available shape".
///
/// **At ejection, not at re-insertion.** Flushing on the way back in leaves the window open for as
/// long as the slot stays empty, and the re-insert path (`deluge_efatfs_remount` ->
/// `AudioFileManager::firstCardRead` -> `cardReinserted`) is about re-validating RESIDENT samples,
/// which is a different question from what a loader has in its tables.
///
/// A load that is genuinely in flight when this is called is also covered: it publishes after this
/// returns, and its result is discarded rather than reinstated. Each loader stamps its results with
/// the card generation the work began under and re-checks that stamp when the result is COLLECTED,
/// so the discard does not depend on this call having managed to empty anything, nor on which
/// context either side runs in. There is nothing for the caller to sequence against and nothing
/// left in a loader to clean up afterwards.
///
/// **That is a statement about the loaders' tables, not about the whole load pipeline.** A result
/// already handed out is past the loader's reach: @ref deluge_audio_load_collect removes it, and the
/// caller then runs its sample build (`storage/audio/sample_build.h`: begin -> bindReadStream ->
/// applyGeometry), of which the bind is a real open. An ejection landing in that
/// window has nothing left to flush -- the geometry is already on the caller's stack. It degrades to a
/// read error rather than a wrong load, because the re-insert (`AudioFileManager::reinitEjectedCard`
/// -> `deluge_efatfs_remount`) cannot run during the bind: it runs from `AudioFileManager::slowRoutine`,
/// a scheduler task on the same tier, and `buildSampleFromLoadResult` runs begin, bind and
/// apply-geometry in one call whose blocking open runs no other task. The open hits the old mount and
/// fails.
///
/// Afterwards every flushed path reports @ref DELUGE_CHANNEL_UNKNOWN, whose documented meaning is
/// already "request it (again)" -- the collector needs no new lifecycle for this.
///
/// Unconditionally callable, and callers must NOT gate it on any loader's "am I active?" selector: a
/// flush skipped on the wrong build is a silent wrong-file load, while one that runs on a build with
/// no tables costs a call.
void deluge_card_flush(void) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_CARD_H
