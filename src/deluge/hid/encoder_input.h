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

/// @brief When the board decoded the select turn being actioned, on `getSystemTime()`'s clock in microseconds.
///
/// `interpretEncoders` hands the select encoder's detents out as the runs of edges that completed them, one
/// `selectEncoderAction` each, and this is that run's time while the call is made: what an accelerated select edit
/// is timed on, as `main`'s `HorizontalMenu::calcNextKnobSpeed` times each turn its encoder task takes as the edges
/// arrive. 0 outside such a call.
uint64_t selectTurnTimeUs();

/// Translate accumulated encoder ticks into UI actions.
///
/// @return true if any encoder produced an action this call.
bool interpretEncoders();

/// The encoder job: actions any pending encoder movement. It runs only when an encoder edge wakes
/// the app loop with DELUGE_WAKE_ENCODERS.
void interpretEncodersTask();

} // namespace deluge::hid::encoders
