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

namespace deluge::storage {

/// @brief Where `SampleRecorder::advanceCreate()` has got to.
///
/// Replaces the `goto` web the create block used to carry (`tryOpenRecordingStream`,
/// `cutRecordingFolderPathAndTryCreating`), which encoded its retry state in control flow.
enum class CreatePhase : uint8_t {
	/// Free-space query. Zero clusters is a terminal `Error::SD_CARD_FULL`.
	Stats,
	/// Ask whichever folder search applies for a path. Spans ticks of its own.
	PathSearch,
	/// Open the recording file.
	Open,
	/// The parent folder was missing. Create it -- ONCE -- and retry the open.
	MakeFolder,
	/// Terminal, success.
	Done,
	/// Terminal, failure.
	Failed,
};

/// @brief Everything `nextCreatePhase` is allowed to look at.
struct CreateInputs {
	CreatePhase phase;
	/// The `DELUGE_FILE_WRITE_*` value from the operation this phase issued, or
	/// `DELUGE_FILE_WRITE_UNKNOWN` when nothing is in flight.
	uint8_t collected;
	/// `Stats` reported zero free clusters.
	bool spaceExhausted;
	/// `PathSearch` has produced a path.
	bool pathReady;
	/// `Open` failed specifically with NOT_FOUND -- the one failure worth retrying.
	bool openNotFound;
	/// `MakeFolder` has already run once for this create.
	bool triedMakeFolder;
	/// Consecutive unproductive ticks (refused, UNKNOWN, PENDING) before this one. See
	/// @ref kMaxUnproductiveTicks.
	int32_t unproductiveTicks;
	/// An operation of this phase was still PENDING past the bound. It fails on its terminal answer,
	/// not before: until then the driver may still be using what it was given.
	bool failing;
};

/// @brief Decide the next phase. PURE -- no I/O, no globals, no clock.
///
/// The ruling worth testing is the retry: a missing parent folder is the only open failure worth
/// acting on, and worth acting on exactly once. Without that bound the machine would alternate
/// between `Open` and `MakeFolder` forever on a path that can never be created.
[[nodiscard]] CreatePhase nextCreatePhase(CreateInputs in);

} // namespace deluge::storage
