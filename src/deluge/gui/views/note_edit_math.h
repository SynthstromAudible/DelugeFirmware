/*
 * Copyright © 2026 Sean Ditny
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
#include <algorithm>
#include <climits>
#include <cstdint>
#include <span>

namespace deluge::gui {
inline int32_t resizeNoteLength(int32_t length, int32_t steps, int32_t zoom, int32_t distanceToNextNote) {
	return std::clamp<int64_t>(static_cast<int64_t>(length) + static_cast<int64_t>(steps) * zoom, 1,
	                           distanceToNextNote);
}

struct NoteMoveRegion {
	int32_t row;
	int32_t position;
	int32_t length;
	int32_t loopLength;
};

// Keep the sub-grid offset when crossing either end of a note row's loop.
inline int32_t wrapNotePosition(int32_t position, int64_t displacement, int32_t loopLength) {
	int64_t wrapped = (position + displacement) % loopLength;
	return wrapped < 0 ? wrapped + loopLength : wrapped;
}

// Find one common grid-aligned displacement before changing any notes. The caller
// enumerates only unselected notes; selected source positions are free to reuse.
// Search at most one revolution of the shortest selected row, jumping past blocked
// intervals. Each row wraps at its own loop length, including independent kit rows.
template <typename ForEachObstacle>
int64_t findNoteGridMove(std::span<const NoteMoveRegion> selected, int32_t steps, int32_t zoom,
                         ForEachObstacle forEachObstacle) {
	if (selected.empty() || !steps || zoom <= 0) {
		return 0;
	}
	const int32_t direction = steps < 0 ? -1 : 1;
	int64_t distance = (steps < 0 ? -static_cast<int64_t>(steps) : steps) * zoom;
	int32_t shortestLoop = INT32_MAX;
	for (const auto& note : selected) {
		if (note.loopLength <= 0 || note.length <= 0 || note.length > note.loopLength) {
			return 0;
		}
		shortestLoop = std::min(shortestLoop, note.loopLength);
	}
	const int64_t limit = distance + shortestLoop;
	while (distance < limit) {
		int64_t advance = 0;
		forEachObstacle([&](const NoteMoveRegion& obstacle) {
			for (const auto& note : selected) {
				if (note.row != obstacle.row) {
					continue;
				}
				const int64_t start = wrapNotePosition(note.position, direction * distance, note.loopLength);
				const int64_t end = start + note.length;
				// Compare neighboring copies of the obstacle on the circular timeline.
				// This protects tails wrapping across zero on either moving or existing notes.
				for (int copy = -1; copy <= 1; ++copy) {
					int64_t obstacleStart = obstacle.position + static_cast<int64_t>(copy) * note.loopLength;
					int64_t obstacleEnd = obstacleStart + obstacle.length;
					if (start < obstacleEnd && end > obstacleStart) {
						advance = std::max(advance, direction > 0 ? obstacleEnd - start : end - obstacleStart);
					}
				}
			}
		});
		if (!advance) {
			bool unchanged = std::all_of(selected.begin(), selected.end(),
			                             [&](const auto& note) { return distance % note.loopLength == 0; });
			return unchanged ? 0 : direction * distance;
		}
		distance += ((advance + zoom - 1) / zoom) * zoom;
	}
	return 0;
}

// Paste never truncates or wraps a copied note. An existing wrapped tail also
// occupies the start of its row, even though its note-on lies near the end.
inline bool notePasteFits(int32_t position, int32_t length, int32_t loopLength) {
	return position >= 0 && length > 0 && static_cast<int64_t>(position) + length <= loopLength;
}

inline bool notePasteOverlaps(int32_t position, int32_t length, int32_t otherPosition, int32_t otherLength,
                              int32_t loopLength) {
	int64_t end = static_cast<int64_t>(position) + length;
	int64_t otherEnd = static_cast<int64_t>(otherPosition) + otherLength;
	return (position < otherEnd && end > otherPosition) || (otherEnd > loopLength && position < otherEnd - loopLength);
}

// Match NoteRow::attemptNoteAdd: occupied heads are skipped; a new tail stops
// at the next existing note. Existing notes themselves never change in gentle mode.
inline int32_t gentleNotePasteLength(int32_t position, int32_t length, int32_t otherPosition, int32_t otherLength,
                                     int32_t loopLength) {
	if (notePasteOverlaps(position, 1, otherPosition, otherLength, loopLength)) {
		return 0;
	}
	return otherPosition > position ? std::min(length, otherPosition - position) : length;
}

inline int32_t noteLengthAfterPasteClear(int32_t position, int32_t length, int32_t loopLength, int32_t clearStart,
                                         int64_t clearEnd) {
	if (clearStart >= loopLength || clearEnd <= clearStart) {
		return length;
	}
	if (position >= clearStart && position < clearEnd) {
		return 0;
	}
	if (position < clearStart) {
		return std::min(length, clearStart - position);
	}
	return std::min<int64_t>(length, static_cast<int64_t>(loopLength) + clearStart - position);
}

// Shared by the shortcut popup and the menu. Pass a copy of offsetFromNextSquare
// when reading, so rendering does not change the ongoing nudge gesture.
inline int32_t noteNudgeDisplayOffset(int32_t offset, int32_t loopLength, int32_t squareWidth, bool alreadyNudged,
                                      bool& offsetFromNextSquare) {
	if (offset >= (loopLength >> 1)) {
		offset -= loopLength;
	}
	else if (offset <= -(loopLength >> 1)) {
		offset += loopLength;
	}
	if (offset > 0) {
		if (offset > (squareWidth >> 1)) {
			if (!alreadyNudged) {
				offsetFromNextSquare = true;
			}
		}
		else {
			offsetFromNextSquare = false;
		}
		if (offsetFromNextSquare) {
			offset -= squareWidth;
		}
	}
	return offset;
}
} // namespace deluge::gui
