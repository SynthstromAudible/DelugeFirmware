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

#include "model/sample/recorder_write_policy.h"

#include "libdeluge/file_write.h"

namespace deluge::storage {

WriteStep nextWriteStep(bool channelActive, bool haveInFlight, uint8_t collected, int32_t pendingTicks) {
	if (!channelActive) {
		return WriteStep::Inline;
	}
	if (!haveInFlight) {
		return WriteStep::Issue;
	}
	// Terminal answers are checked BEFORE the stall bound. A READY arriving on the tick the bound
	// expires still means the bytes are on the card, and discarding it would rewrite the cluster
	// from a buffer the completion path is about to recycle.
	switch (collected) {
	case DELUGE_FILE_WRITE_READY:
		return WriteStep::Complete;
	case DELUGE_FILE_WRITE_FAILED:
		return WriteStep::Failed;
	case DELUGE_FILE_WRITE_UNKNOWN:
		// "Request again", NOT "keep waiting" -- the request was never keyed.
		return WriteStep::Issue;
	default:
		break;
	}
	return pendingTicks >= kMaxPendingTicks ? WriteStep::Inline : WriteStep::Wait;
}

} // namespace deluge::storage
