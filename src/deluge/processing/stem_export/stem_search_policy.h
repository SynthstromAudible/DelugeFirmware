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

/// @brief Where `StemExport`'s free-folder search has got to.
enum class StemSearchPhase : uint8_t {
	/// Create the recording-folder root (SAMPLES/EXPORTS).
	MkdirBase,
	/// Create the per-song folder under it.
	MkdirSong,
	/// Ask whether this numbered candidate already exists.
	ProbeCandidate,
	/// The candidate is proven absent: take it.
	MkdirCandidate,
	/// Terminal, success.
	Done,
	/// Terminal, failure -- including "could not tell", which must never be read as "free".
	Failed,
};

/// @brief Candidates tried before the search gives up.
///
/// The loop this replaces was UNBOUNDED. A directory that somehow answers Present forever would
/// otherwise search indefinitely, and a stem export that never starts is worse than one that
/// refuses.
constexpr int32_t kMaxStemCandidates = 1000;

/// @brief Everything `nextStemSearchPhase` is allowed to look at.
struct StemSearchInputs {
	StemSearchPhase phase;
	/// The `DELUGE_FILE_WRITE_*` value from the operation this phase issued, or
	/// `DELUGE_FILE_WRITE_UNKNOWN` when nothing is in flight.
	uint8_t collected;
	/// `ProbeCandidate`'s answer.
	Presence candidate;
	int32_t candidatesTried;
};

/// @brief Decide the next phase. PURE -- no I/O, no globals, no clock.
///
/// Two rulings here are about not stealing somebody's finished export, and both are invisible when
/// wrong: only a candidate proven `Absent` may be created (efatfs `mkdir` succeeds on one that
/// already exists, so create-then-check would adopt an occupied folder), and `Undeterminable`
/// aborts rather than counting as free.
[[nodiscard]] StemSearchPhase nextStemSearchPhase(StemSearchInputs in);

} // namespace deluge::storage
