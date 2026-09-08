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

#include "model/sample/recorder_finalize_policy.h"

#include "libdeluge/file_write.h"

namespace deluge::storage {

namespace {
/// The phases that issue a channel operation and must therefore respect its collect status. The
/// pure phases below (`Decide`, `Bookkeeping`) advance regardless, because they issue nothing.
bool issuesIo(FinalizePhase p) {
	switch (p) {
	case FinalizePhase::Decide:
	case FinalizePhase::Bookkeeping:
	case FinalizePhase::Done:
	case FinalizePhase::Failed:
		return false;
	default:
		return true;
	}
}
} // namespace

FinalizePhase nextFinalizePhase(FinalizeInputs in) {
	// Terminal states absorb everything, including a late collect.
	if (in.phase == FinalizePhase::Done || in.phase == FinalizePhase::Failed) {
		return in.phase;
	}

	if (issuesIo(in.phase)) {
		switch (in.collected) {
		case DELUGE_FILE_WRITE_FAILED:
			// From EVERY phase. Falling through to the next one would finalize a file over a write
			// that did not happen.
			return FinalizePhase::Failed;
		case DELUGE_FILE_WRITE_PENDING:
			// Hold position: advancing here would issue the next operation while this one still owns
			// the caller's buffer.
			return in.phase;
		case DELUGE_FILE_WRITE_UNKNOWN:
			// The request was never keyed. Stay so the caller re-issues -- NOT the same as PENDING,
			// and conflating them strands finalize waiting for an answer nobody owes it.
			return in.phase;
		default:
			break; // READY: fall through to the per-phase transitions.
		}
	}

	switch (in.phase) {
	case FinalizePhase::DrainClusters:
		return in.clustersRemain ? FinalizePhase::DrainClusters : FinalizePhase::WriteFinalPartial;

	case FinalizePhase::WriteFinalPartial:
		return FinalizePhase::Decide;

	case FinalizePhase::Decide:
		if (in.altering) {
			return FinalizePhase::CloseBeforeAlter;
		}
		// Plain: truncate FIRST, then patch the header of the file we just shortened.
		if (in.needTruncate) {
			return FinalizePhase::Truncate;
		}
		return in.needHeaderPatch ? FinalizePhase::HeaderRead : FinalizePhase::Close;

	case FinalizePhase::CloseBeforeAlter:
		return FinalizePhase::OpenForAlter;

	case FinalizePhase::OpenForAlter:
		return FinalizePhase::HeaderRead;

	case FinalizePhase::HeaderRead:
		return FinalizePhase::HeaderWrite;

	case FinalizePhase::HeaderWrite:
		// Altering: the header is written before the batches, because the batches rewrite the audio
		// the header describes. Plain: the header patch is the last thing before the close.
		if (in.altering) {
			return in.framesRemain ? FinalizePhase::BatchRead : FinalizePhase::Close;
		}
		return FinalizePhase::Close;

	case FinalizePhase::BatchRead:
		return FinalizePhase::BatchWrite;

	case FinalizePhase::BatchWrite:
		if (in.framesRemain) {
			return FinalizePhase::BatchRead;
		}
		// Altering: truncate AFTER the batches -- the final length is not known until they have run.
		return in.needTruncate ? FinalizePhase::Truncate : FinalizePhase::Close;

	case FinalizePhase::Truncate:
		if (in.altering) {
			return FinalizePhase::Close;
		}
		return in.needHeaderPatch ? FinalizePhase::HeaderRead : FinalizePhase::Close;

	case FinalizePhase::Close:
		return FinalizePhase::Bookkeeping;

	case FinalizePhase::Bookkeeping:
		return FinalizePhase::Done;

	case FinalizePhase::Done:
	case FinalizePhase::Failed:
		return in.phase;
	}
	return FinalizePhase::Failed;
}

} // namespace deluge::storage
