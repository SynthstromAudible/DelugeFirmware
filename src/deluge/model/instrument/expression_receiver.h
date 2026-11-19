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

/// @file
/// @brief Who hears a change to an expression lane, found from the lane's address (its clip and note row).

class Drum;
class InstrumentClip;
class MelodicInstrument;
class NoteRow;

namespace deluge::model {

/// @brief Who hears a change to an expression lane: at most one of the two is set.
struct ExpressionReceiver {
	MelodicInstrument* melodic = nullptr; ///< a melodic clip's output: its channel lanes and its rows' per-note lanes
	Drum* drum = nullptr;                 ///< a kit row's drum: that row's per-note lanes
};

/// @brief The receiver of @p clip's channel expression lanes, or of @p noteRow's per-note lanes when it is set.
///
/// A kit's own lanes and a kit row without a drum have none.
[[nodiscard]] ExpressionReceiver expressionReceiverOf(const InstrumentClip& clip, const NoteRow* noteRow);

} // namespace deluge::model
