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

/// @brief Where `SampleRecorder::advanceFinalize()` has got to.
///
/// Every phase except `Decide`, `Bookkeeping`, `Done` and `Failed` issues exactly one channel
/// operation and waits for its collect. The two branches -- altering and plain -- diverge at
/// `Decide` and rejoin at `Bookkeeping`.
enum class FinalizePhase : uint8_t {
	/// Write out completed clusters, one per tick, until none remain.
	DrainClusters,
	/// Write the partially-filled final cluster, if there is one.
	WriteFinalPartial,
	/// PURE: work out the monitoring action and normalization shift from the recorded peaks.
	Decide,
	/// Altering only: close the recording stream before reopening it for the transform.
	CloseBeforeAlter,
	/// Altering only: reopen the file for append.
	OpenForAlter,
	/// Read the header back through the open write context.
	HeaderRead,
	/// Patch the header in the scratch buffer and write it back.
	HeaderWrite,
	/// Altering only: read one batch of frames.
	BatchRead,
	/// Altering only: write the transformed batch back.
	BatchWrite,
	/// Shorten the file to its true length.
	Truncate,
	/// Close the stream, flushing the deferred directory entry.
	Close,
	/// PURE: final channel count, sample length, and the overview-cache grow.
	Bookkeeping,
	/// Terminal, success.
	Done,
	/// Terminal, failure. The caller sets `hadCardError`.
	Failed,
};

/// @brief Everything `nextFinalizePhase` is allowed to look at.
struct FinalizeInputs {
	FinalizePhase phase;
	/// The `DELUGE_FILE_WRITE_*` value from the operation this phase issued, or
	/// `DELUGE_FILE_WRITE_UNKNOWN` when nothing is in flight.
	uint8_t collected;
	bool clustersRemain;
	bool havePartialBuffer;
	bool altering;
	bool framesRemain;
	bool needTruncate;
	bool needHeaderPatch;
};

/// @brief Decide the next phase. PURE -- no I/O, no globals, no clock.
///
/// Pure so every ruling is testable without fault injection, which is what made the startup phase
/// machine's conflation rulings provable rather than argued. Two of the rulings here are invisible
/// when wrong and have their own spec cases: a FAILED collect must reach @ref FinalizePhase::Failed
/// from every phase rather than falling through, and the two branches truncate at OPPOSITE ends of
/// their sequences.
[[nodiscard]] FinalizePhase nextFinalizePhase(FinalizeInputs in);

} // namespace deluge::storage
