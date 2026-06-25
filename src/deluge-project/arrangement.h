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

#include "definitions_cxx.hpp"    // kMaxNumSections
#include "io/midi/learned_midi.h" // LearnedMIDI (held by value in Section)
#include <array>
#include <cstdint>

class Clip;
class Output;

namespace deluge::project {

/// A launchable song section: how many times it repeats and an optional MIDI-learned launch command.
class Section {
public:
	LearnedMIDI launchMIDICommand;
	int16_t numRepetitions{0};
};

/// What one of the assignable session macros does when triggered.
enum SessionMacroKind : int8_t {
	NO_MACRO = 0,
	CLIP_LAUNCH,
	OUTPUT_CYCLE,
	SECTION,
	NUM_KINDS,
};

/// One assignable session macro: its kind plus the clip/output/section it targets.
struct SessionMacro {
	SessionMacroKind kind{NO_MACRO};
	Clip* clip{nullptr};
	Output* output{nullptr};
	uint8_t section{0};
};

/// Song arrangement structure: the launch sections and the eight assignable session macros.
/// A component of deluge::project::Song.
struct Arrangement {
	Section sections[kMaxNumSections];
	std::array<SessionMacro, 8> sessionMacros{};
};

} // namespace deluge::project
