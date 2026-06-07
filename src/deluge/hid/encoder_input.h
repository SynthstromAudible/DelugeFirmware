/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

namespace deluge::hid::encoders {

/// Last AudioEngine::audioSampleTimer tick at which a mod encoder changed.
/// Written here and by view.cpp to reset the "turned recently" window.
extern uint32_t timeModEncoderLastTurned[];

/// Translate accumulated encoder ticks into UI actions.
///
/// @param skipActioning  When true, drains and clears encoder state but suppresses most UI actions
///                       (used during SD card I/O and audio engine yield paths).
/// @return true if any encoder produced an action this call.
bool interpretEncoders(bool skipActioning = false);

/// Scheduler task entry point: actions any pending encoder movement, then re-blocks itself so it
/// won't run again until the encoder IRQ unblocks it.
void interpretEncodersTask();

} // namespace deluge::hid::encoders
