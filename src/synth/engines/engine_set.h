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

#include "definitions_cxx.hpp" // SynthMode
#include "synth/engines/engine.h"
#include "synth/engines/fm_engine.h"
#include "synth/engines/ring_mod_engine.h"
#include "synth/engines/subtractive_engine.h"

namespace deluge::synth {

/// @brief One of each engine a voice can play, and the factory that picks one by synth mode. Every engine keeps its
/// state while another plays, so a voice whose sound changes mode while it sounds (an undo, a preset applied) carries
/// on from it as before.
class EngineSet {
public:
	/// The engine for @p mode.
	[[nodiscard]] Engine& for_mode(SynthMode mode) {
		switch (mode) {
		case SynthMode::RINGMOD:
			return ring_mod_;
		case SynthMode::FM:
			return fm_;
		default:
			return subtractive_;
		}
	}
	/// FM, whose modulator levels the voice keeps current in every mode.
	[[nodiscard]] FMEngine& fm() { return fm_; }

private:
	SubtractiveEngine subtractive_;
	RingModEngine ring_mod_;
	FMEngine fm_;
};

} // namespace deluge::synth
