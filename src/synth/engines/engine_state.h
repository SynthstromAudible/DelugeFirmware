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

#include "synth/sources/source_state.h"

#include <array>

namespace deluge::synth {

/// @brief Two oscillator slots, the second hard-synced to the first when @c sync.
struct DualSourceState {
	std::array<SlotState, 2> slots{VaState{}, VaState{}};
	bool sync{false};
	bool operator==(const DualSourceState&) const = default;
};

/// @brief What the subtractive engine saves.
struct SubtractiveState {
	DualSourceState oscillators;
	bool operator==(const SubtractiveState&) const = default;
};

/// @brief What the ring-mod engine saves: two oscillators of the kinds ring modulation takes.
struct RingModState {
	DualSourceState oscillators;
	bool operator==(const RingModState&) const = default;
};

/// @brief What the FM engine saves: two carriers and two modulators, the second modulating the first when
/// @c modulator1_to_modulator0, and the oscillators' sync, which the file writes in the second carrier's pitch.
struct FmState {
	std::array<Pitch, 2> carriers{};
	bool sync{false};
	/// A modulator restarts each note at phase 0 unless set to run free.
	std::array<Pitch, 2> modulators{Pitch{.tuning = {}, .retrigger = dsp::UQ<32>{}},
	                                Pitch{.tuning = {.transpose = -12}, .retrigger = dsp::UQ<32>{}}};
	bool modulator1_to_modulator0{false};
	bool operator==(const FmState&) const = default;
};

} // namespace deluge::synth
