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

#include "model/sample/recorder_create_policy.h"

#include "libdeluge/file_write.h"

namespace deluge::storage {

CreatePhase nextCreatePhase(CreateInputs in) {
	if (in.phase == CreatePhase::Done || in.phase == CreatePhase::Failed) {
		return in.phase;
	}
	if (in.collected == DELUGE_FILE_WRITE_PENDING || in.collected == DELUGE_FILE_WRITE_UNKNOWN) {
		// PENDING: the answer is coming. UNKNOWN: it was never asked. Both mean stay -- the caller
		// re-issues on UNKNOWN and simply waits on PENDING.
		return in.phase;
	}
	if (in.collected == DELUGE_FILE_WRITE_FAILED) {
		// A missing parent folder is the one failure worth acting on, and exactly once. Every other
		// failure is terminal here: making a folder would not have helped.
		if (in.phase == CreatePhase::Open && in.openNotFound && !in.triedMakeFolder) {
			return CreatePhase::MakeFolder;
		}
		return CreatePhase::Failed;
	}
	switch (in.phase) {
	case CreatePhase::Stats:
		return in.spaceExhausted ? CreatePhase::Failed : CreatePhase::PathSearch;
	case CreatePhase::PathSearch:
		return in.pathReady ? CreatePhase::Open : CreatePhase::PathSearch;
	case CreatePhase::Open:
		return CreatePhase::Done;
	case CreatePhase::MakeFolder:
		return CreatePhase::Open;
	case CreatePhase::Done:
	case CreatePhase::Failed:
		return in.phase;
	}
	return CreatePhase::Failed;
}

} // namespace deluge::storage
