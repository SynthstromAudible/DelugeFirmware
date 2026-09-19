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

#include "model/sample/recorder_write_policy.h" // UnproductiveSpan, kMaxUnproductiveUs

#include <cstdint>

namespace deluge::storage {

/// @brief Where the re-check of a returned card has got to.
enum class CardReinsertPhase : uint8_t {
	/// No card has come back that still needs checking.
	Idle,
	/// Walking the resident audio files, one per tick.
	Walk,
	/// Terminal.
	Done,
};

/// @brief Re-checks every resident sample after a card comes back, one per tick.
///
/// What this replaces was a single uninterrupted pass that opened EVERY resident sample's file
/// synchronously, which is why it had to be handed to the storage owner. A card holding many samples
/// blocked for the whole walk. Now one resident is checked per tick, through the byte channel, so the
/// work happens where its caller stands.
///
/// @note The check is `bytes::requestExists`/`collectExists`, and that is not a weakening of the
///       blocking `Stream::open()` it replaces: `requestExists` OPENS ITS PATH AS A FILE (see
///       byte_channel.h's note on @ref requestDirExists), so the two ask the same question.
///
/// @warning `Failed` is NOT absence. `collectExists` is three-state: `Ready` with `present == false`
///          is "known absent", while `Failed` is "could not establish". Marking a sample unloadable
///          on `Failed` would re-create the defect fixed in `f37a8ef41`, where `sector_of()`'s
///          deliberate "I cannot answer" was read as "the file is gone" and every sample went silent
///          on every re-insertion. Only a definite absence may mark one.
///
/// Supersede follows @ref SampleClaim's shape: a card that comes back again mid-walk re-arms rather
/// than restarting immediately, because an outstanding channel request is keyed to a path and its
/// collect must be consumed before another is issued.
class CardReinsert {
public:
	/// @brief A card came back. Starts the walk, or re-arms one already running.
	void arm();

	/// @brief Advance by at most one resident. Returns true while there is still work.
	bool tick();

	[[nodiscard]] bool running() const { return phase_ == CardReinsertPhase::Walk; }

	/// @name Lenses for the `card-reinsert` gate.
	/// @{
	/// Residents whose existence the channel could not establish. A card answering nothing is a
	/// different fact from a card whose files are gone, and must not look like one.
	[[nodiscard]] int32_t unestablished() const { return unestablished_; }
	/// Completed walks, so a caller can tell a finished re-check from one that never started.
	[[nodiscard]] int32_t walks() const { return walks_; }
	/// @}

private:
	CardReinsertPhase phase_{CardReinsertPhase::Idle};
	/// Index into `AudioFileManager::audioFiles`. Not advanced after an eviction: the container
	/// closes up behind the removed element, exactly as the old loop's `e--` accounted for.
	int32_t cursor_{0};
	/// Whether this resident's existence request has been issued and is awaiting its collect.
	bool issued_{false};
	/// A card came back while a walk was still running: restart once this resident settles.
	bool rearm_{false};
	/// How long the current resident's request has gone unanswered. Elapsed time, not ticks -- see
	/// recorder_write_policy.h for why a tick count means a different wait to every caller.
	UnproductiveSpan span_;
	int32_t unestablished_{0};
	int32_t walks_{0};
};

} // namespace deluge::storage

extern deluge::storage::CardReinsert cardReinsert;
