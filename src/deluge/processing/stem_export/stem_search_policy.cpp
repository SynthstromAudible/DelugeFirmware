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

#include "processing/stem_export/stem_search_policy.h"

#include "libdeluge/file_write.h"

namespace deluge::storage {

StemSearchPhase nextStemSearchPhase(StemSearchInputs in) {
	if (in.phase == StemSearchPhase::Done || in.phase == StemSearchPhase::Failed) {
		return in.phase;
	}
	if (in.collected == DELUGE_FILE_WRITE_PENDING || in.collected == DELUGE_FILE_WRITE_UNKNOWN) {
		return in.phase;
	}
	if (in.collected == DELUGE_FILE_WRITE_FAILED) {
		// Unlike the create machine, no failure here has a retry: a folder that cannot be made is
		// the end of the search, and reusing one is exactly what must not happen.
		return StemSearchPhase::Failed;
	}
	switch (in.phase) {
	case StemSearchPhase::MkdirBase:
		return StemSearchPhase::MkdirSong;
	case StemSearchPhase::MkdirSong:
		return StemSearchPhase::ProbeCandidate;
	case StemSearchPhase::ProbeCandidate:
		switch (in.candidate) {
		case Presence::Absent:
			// Proven absent is the ONLY safe case to take: efatfs mkdir is idempotent, so creating
			// first and checking the result would happily adopt an occupied export folder.
			return StemSearchPhase::MkdirCandidate;
		case Presence::Present:
			// Occupied -- try the next number, unless we have tried too many.
			return in.candidatesTried >= kMaxStemCandidates ? StemSearchPhase::Failed : StemSearchPhase::ProbeCandidate;
		case Presence::Undeterminable:
			// Could not tell. Refusing is always safe; reusing is not.
			return StemSearchPhase::Failed;
		}
		return StemSearchPhase::Failed;
	case StemSearchPhase::MkdirCandidate:
		return StemSearchPhase::Done;
	case StemSearchPhase::Done:
	case StemSearchPhase::Failed:
		return in.phase;
	}
	return StemSearchPhase::Failed;
}

} // namespace deluge::storage
