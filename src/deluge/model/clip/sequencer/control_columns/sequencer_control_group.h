/*
 * Copyright © 2024 Synthstrom Audible Limited
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
#include "gui/ui/ui.h"
#include <array>

namespace deluge::model::clip::sequencer {

// Control types available for sequencer columns
enum class ControlType {
	NONE, // Empty/unused pad (always first)
	// Alphabetically sorted from here:
	CLOCK_DIV, // Clock divider/multiplier
	DIRECTION, // Playback direction
	EVOLVE,    // Generative: evolve pattern (gentle at low %, chaotic at high %)
	OCTAVE,    // Octave shift
	RANDOM,    // Generative: complete randomization (with % intensity)
	RESET,     // Generative: reset to init (no value)
	SCENE,     // Scene capture/recall
	TRANSPOSE, // Semitone transpose
	MAX
};

// Pad behavior mode
enum class PadMode {
	TOGGLE,   // Press to activate, press again to deactivate
	MOMENTARY // Hold to activate, release to deactivate
};

} // namespace deluge::model::clip::sequencer
