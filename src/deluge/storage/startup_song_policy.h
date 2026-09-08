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

#include "storage/existence_policy.h"

#include <cstdint>

namespace deluge::storage {

/// @brief Where the startup fail-safe machine has got to.
///
/// The machine advances one phase per tick of `startupSongRoutine()`. It exists because the canary
/// write must be OBSERVED to have landed before the load begins, and observing a request/collect
/// answer takes a later tick than issuing it.
enum class StartupPhase : uint8_t {
	Idle,                  ///< Not armed. The conditional task moves it to `CheckCanary`.
	CheckCanary,           ///< Is a canary from a previous boot still there?
	CheckTemplate,         ///< Template mode only: does `SONGS/DEFAULT.XML` exist?
	WriteCanary,           ///< Create the canary, and WAIT for it to land.
	CheckSong,             ///< Is the song we mean to load actually there?
	CheckFallbackTemplate, ///< The song is gone: is the template there instead?
	BeginLoad,             ///< Hand off to `LoadSongUI`. The completion owns the cleanup.
	UnlinkCanary,          ///< Remove the canary, then finish. This was not a crash.
	Done,                  ///< Finished, one way or another. The routine does nothing from here.
};

/// @brief One transition of the fail-safe machine, and the mode it leaves behind.
struct StartupStep {
	StartupPhase next;
	/// Whether the song about to be loaded is the TEMPLATE. Carried through every transition because
	/// the load's completion wipes the song's name when it is true, so that Save asks for a new name
	/// rather than overwriting the template.
	bool usingTemplate;
};

/// @brief Decide the next phase from an existence answer. PURE — no I/O, no globals, no UI.
///
/// Pure deliberately, and it is the whole reason the four conflation rulings are testable at all: a
/// real card answers `Confirmed` or `Missing` and never `Unusable`, so driving the third state
/// through a live machine would need a fault-injection seam. Through this function it needs nothing.
///
/// @param current       The phase whose check just answered.
/// @param answer        What @ref startup_action made of that check.
/// @param usingTemplate Whether the song in play is the template, coming in.
/// @return The next phase and the outgoing template mode.
///
/// @note Every `Unusable` answer routes to `Done` or `UnlinkCanary` — never to a phase that writes.
///       That is the rule the four `TODO(conflation-batch-3)` sites currently break.
StartupStep nextStartupPhase(StartupPhase current, StartupPresence answer, bool usingTemplate);

} // namespace deluge::storage
