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

#include "definitions_cxx.hpp"

#include <cstdint>

/// Checks on the playhead: the render ticks the sequencer, so every write to playback position state the interaction
/// tier makes while a clock runs must exclude the render, and what runs with the render excluded must not allocate or
/// log undo.
namespace deluge::playback {

#if ALPHA_OR_BETA_VERSION
/// Playback-position writes made off the render while a clock is active and no AudioExcludedSection is held: a render
/// block could have seen them half made. Must stay 0.
extern uint32_t strayPositionWrites;
/// Actions (undo) begun on the interaction tier inside a playhead move's section (a NoAllocationScope). Must stay 0.
extern uint32_t maskedActions;

/// @brief [render] or [interaction] Count a write to playback position state (a lane cursor, a clip's or note row's
///        position, the next-event schedule, the playback mode or state) if the render could see it half made.
void notePositionWrite();
/// @brief [render] or [interaction] Count an Action begun inside a playhead move's section.
void noteActionBegun();
#else
inline void notePositionWrite() {
}
inline void noteActionBegun() {
}
#endif

} // namespace deluge::playback
