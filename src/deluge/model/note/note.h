/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "model/iterance/iterance.h"
#include "model/positionable.h"
#include "model/scale/anchor_marker.h"
#include <cstdint>

class Note : public Positionable {
public:
	Note();

	inline void setLength(int32_t newLength) { length = newLength; }

	inline int32_t getLength() { return length; }

	inline void setVelocity(int32_t newVelocity) { velocity = newVelocity; }

	inline int32_t getVelocity() { return velocity; }

	inline void setLift(int32_t newLift) { lift = newLift; }

	inline int32_t getLift() { return lift; }

	inline void setProbability(int32_t newProbability) { probability = newProbability; }

	inline int32_t getProbability() { return probability; }

	inline void setIterance(Iterance newIterance) { iterance = newIterance; }

	inline Iterance getIterance() { return iterance; }

	inline void setFill(int32_t newFill) { fill = newFill; }

	inline int32_t getFill() { return fill; }

	inline bool isDrone(int32_t effectiveLength) { return (pos == 0 && length == effectiveLength); }

	inline void setLockSet(uint16_t newLockSet) { lockSet = newLockSet; }

	inline uint16_t getLockSet() { return lockSet; }

	//	void writeToFile();
	int32_t length;
	uint8_t velocity;
	uint8_t probability;
	/// Parameter locks: index into the owning Song's deluge::note_locks::Pool, 0 for none. See note_locks.h.
	/// Sits in what was padding before the 4-aligned iterance, so it costs no memory. Notes are created in raw
	/// NoteVector memory without running a constructor - NoteVector's insert functions zero this.
	uint16_t lockSet;
	Iterance iterance;
	uint8_t fill;
	/// Lift is noteOff velocity.
	uint8_t lift;
	/// Accidental lineage: the in-key pitch this note hangs off, when that can't be derived from the row's pitch and
	/// the key (see AnchorMarker for the encodings, and model/scale/accidentals.h). Written by scale changes, which
	/// move notes by their anchors; a note moved to another row by hand is a new Note, so starts out derived. Must be
	/// carried along wherever a note's other fields are copied one by one. Sits in what was trailing padding. Notes
	/// live in raw NoteVector memory, so NoteVector resets it on insert.
	AnchorMarker anchorMarker;

	// Understanding the probability field: the first bit says whether it's based on a previous one.
	// So take the rightmost 7 bits. If that's greater than kNumProbabilityValues (20),
};
