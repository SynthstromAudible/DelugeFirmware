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

#include "model/scale/accidentals.h"
#include "util/containers.h"
#include <cstdint>

class InstrumentClip;
class ModelStackWithTimelineCounter;
class NoteRow;
class ScaleChange;

/** Moves the notes of a scale-mode clip for a scale change which shifts or remaps scale degrees.
 *
 * Notes are moved one at a time (see model/scale/accidentals.h), so that notes which an earlier scale change put in
 * the same NoteRow split back out again. In-key notes go where their scale degree goes; accidentals (notes outside the
 * scale, or carrying an AnchorMarker) go where their anchor takes them. Rows can then land on or cross each other, so
 * the rows are sorted again and rows with the same pitch merged.
 *
 * None of this is undoable.
 */
class NoteRegrouper {
public:
	/// The song's key (modelStack->song->key) must still be the one from before the change.
	NoteRegrouper(InstrumentClip& clip, ModelStackWithTimelineCounter* modelStack)
	    : clip_(clip), modelStack_(modelStack) {}

	/** Applies the change, whose target is the scale after it. Doesn't touch the clip's yScroll.
	 *
	 * pendingTranspose is a uniform shift in semitones which the caller is still to apply to every row and to the
	 * key's root afterwards (in-key transposition does this): markers are chosen for the key as it will be then.
	 *
	 * Returns whether rows were restructured: notes changed NoteRow, or rows merged. Undo history made before that
	 * finds its notes by row, so it is stale and must not be reverted (see ActionLogger::invalidateLogs()).
	 */
	[[nodiscard]] bool regroup(const ScaleChange& change, deluge::accidentals::AccidentalHandling handling,
	                           int32_t pendingTranspose = 0);

private:
	/// One flag per NoteRow: whether its pitch was in the key before the change
	using RowFlags = deluge::vector<uint8_t>;

	bool moveWholeRows(const ScaleChange& change, const MusicalKey& oldKey, const MusicalKey& finalKey,
	                   int32_t pendingTranspose);
	bool sortAndMergeRows(RowFlags& wasInScale);
	void moveRow(NoteRow* row, int32_t rowIndex, int32_t newY);

	InstrumentClip& clip_;
	ModelStackWithTimelineCounter* modelStack_;
};
