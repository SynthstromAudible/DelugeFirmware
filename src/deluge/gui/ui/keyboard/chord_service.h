/*
 * Copyright © 2016-2025 Synthstrom Audible Limited
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

namespace deluge::gui::ui::keyboard {

constexpr uint8_t kMaxPendingChordNotes = 16;

/// @brief A captured chord, represented as a plain list of absolute MIDI notes.
///
/// Deliberately source-agnostic: it holds resolved notes, NOT a chord index/voicing, so any
/// chord-producing mode (Chord Library, Chord, future chord memory / packs / favorites) can
/// capture into it from its currently-sounding notes. Placement consumes this and does not care
/// where the notes came from. See docs/HARMONIC_COMPOSITION_ARCHITECTURE.md.
struct PendingChord {
	int16_t notes[kMaxPendingChordNotes] = {};
	uint8_t count = 0;
	uint8_t velocity = 64;
};

/// @brief UI-agnostic store + placement for a Pending Chord.
///
/// Capture happens in a keyboard mode; placement happens later in the piano roll. The pending
/// chord lives here (module state), so it survives the keyboard-view -> clip-view switch.
namespace ChordService {
/// Store @p chord as the pending chord and show a "PEND" confirmation. No-op if it has no notes.
void capturePending(const PendingChord& chord);

/// True if there is a pending chord awaiting placement.
bool hasPending();

/// Discard the pending chord (e.g. on cancel).
void clearPending();

/// Place the pending chord's notes into the current melodic clip at tick @p pos, each @p length
/// ticks long, as one undoable action. Clears the pending chord and shows "PLCD" on success.
/// Returns true if at least one note was placed.
bool placePendingAt(int32_t pos, int32_t length);

/// Copy the pending chord's notes into @p notesOut (up to @p maxNotes) and its velocity into
/// @p velocityOut. Returns the number of notes (0 if nothing is pending). For audible preview.
uint8_t getPendingNotes(int16_t* notesOut, uint8_t maxNotes, uint8_t* velocityOut);

/// Re-voice the pending chord by ear, operating on the resolved notes (NOT chord theory), so it
/// works on any captured payload. Positive @p steps move the lowest note up an octave; negative
/// move the highest note down an octave. Mutates the pending chord in place.
void voicingCycle(int8_t steps);
} // namespace ChordService

} // namespace deluge::gui::ui::keyboard
