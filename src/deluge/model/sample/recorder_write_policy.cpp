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

bool stalledPastBound(uint8_t collected, int32_t unproductiveTicks) {
	return collected == DELUGE_FILE_WRITE_PENDING && unproductiveTicks >= kMaxUnproductiveTicks;
}

WriteStep nextWriteStep(bool channelUsable, bool issued, uint8_t collected, int32_t unproductiveTicks) {
	if (!channelUsable) {
		return WriteStep::Inline;
	}
	const bool exhausted = unproductiveTicks >= kMaxUnproductiveTicks;
	if (!issued) {
		// Owed: never asked, or refused. Nothing is outstanding, so giving up releases nothing the driver holds.
		return exhausted ? WriteStep::Failed : WriteStep::Issue;
	}
	// Terminal answers are checked BEFORE the bound. A READY past it still means the bytes are on the card,
	// and the answer is what makes the buffer safe to release.
	switch (collected) {
	case DELUGE_FILE_WRITE_READY:
		return WriteStep::Complete;
	case DELUGE_FILE_WRITE_FAILED:
		return WriteStep::Failed;
	case DELUGE_FILE_WRITE_UNKNOWN:
		// "Request again", NOT "keep waiting" -- the request is not queued or running. At the bound nothing is
		// outstanding, so it fails.
		return exhausted ? WriteStep::Failed : WriteStep::Issue;
	default:
		break;
	}
	// Still in flight. Past the bound the take fails now, and the buffer is released later.
	return stalledPastBound(collected, unproductiveTicks) ? WriteStep::StalledWait : WriteStep::Wait;
}

} // namespace deluge::storage
