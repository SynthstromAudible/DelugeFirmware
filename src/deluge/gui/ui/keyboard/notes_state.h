/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "util/misc.h"
#include <climits>
#include <cstdint>
#include <cstring>
#include <ranges>

constexpr uint8_t kMaxNumActiveNotes = 10;

namespace deluge::gui::ui::keyboard {

struct PressedPad : Cartesian {
	uint32_t timeLastPadPress;
	bool padPressHeld;
	bool active;
	// all evaluatePads will be called at least once with pad.active == false on release. Following
	// that, dead will be set to true to avoid repeatedly processing releases.
	// exception - if the pad is "used up" by switching keyboard columns it will be set dead immediately to prevent
	// processing its release, while still being set as active
	bool dead;
};

struct NoteState {
	uint8_t note = 0;
	/// Number of times this note has been activated.
	/// Used to detect retriggers
	uint8_t activationCount = 0;
	uint8_t velocity = 0;
	int16_t mpeValues[3] = {0};
	/// Generated notes will only create sound and not be used for interaction (e.g. setting root note)
	bool generatedNote = false;
};

// Always needs to be 0 currently for the math to math
constexpr uint8_t kLowestKeyboardNote = 0;
constexpr uint8_t kHighestKeyboardNote = kOctaveSize * 12;
struct NotesState {
	using NoteArray = std::array<NoteState, kMaxNumActiveNotes>;

	uint64_t states[util::div_ceil(size_t{kHighestKeyboardNote}, size_t{64})] = {0};
	NoteArray notes;
	uint8_t count = 0;

	[[nodiscard]] NoteArray::iterator begin() { return notes.begin(); }

	[[nodiscard]] NoteArray::iterator end() { return notes.begin() + count; }

	[[nodiscard]] NoteArray::const_iterator begin() const { return notes.begin(); }

	[[nodiscard]] NoteArray::const_iterator end() const { return notes.end() + count; }

	NoteArray::size_type enableNote(uint8_t note, uint8_t velocity, bool generatedNote = false,
	                                int16_t* mpeValues = nullptr) {
		if (noteEnabled(note)) {
			for (auto i = 0; i < notes.size(); ++i) {
				auto& noteState = notes[i];
				if (noteState.note == note) {
					noteState.activationCount++;
					return i;
				}
			}
		}
		if (count == kMaxNumActiveNotes) {
			return count;
		}

		NoteArray::size_type idx = count++;
		NoteState& state = notes[idx];
		state.note = note;
		state.velocity = velocity;
		state.generatedNote = generatedNote;
		if (mpeValues != nullptr) {
			memcpy(&state.mpeValues, mpeValues, sizeof(state.mpeValues));
		}

		states[(note / 64)] |= (1ull << (note % 64));

		return idx;
	}

	bool noteEnabled(uint8_t note) {
		uint64_t expectedValue = (1ull << (note % 64));
		return (states[(note / 64)] & expectedValue) == expectedValue;
	}
};

}; // namespace deluge::gui::ui::keyboard
