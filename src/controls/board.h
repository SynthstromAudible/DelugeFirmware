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
/// @brief What a board offers the controls layer, and the map from its physical controls to the logical ones the
///        layer's rules name.

#include <cstdint>
#include <optional>

namespace deluge::controls {

/// @brief What a board offers the controls layer.
struct Board {
	uint8_t sections = 0;    ///< section buttons; one section each
	uint8_t slots = 0;       ///< slots a board turns per section (its gold knobs)
	bool slot_press = false; ///< a slot's knob can be pressed
	uint8_t pad_columns = 0; ///< the main pad grid
	uint8_t pad_rows = 0;
	uint8_t sidebar_columns = 0;
	uint8_t indicator_leds = 0; ///< LEDs per slot indicator
};

/// @brief A physical control, numbered as the board numbers it.
struct Physical {
	enum class Kind : uint8_t { SECTION_BUTTON, SLOT_ENCODER, SLOT_PRESS } kind = Kind::SECTION_BUTTON;
	uint8_t index = 0;
};

/// @brief The logical control a physical one drives.
struct Logical {
	enum class Kind : uint8_t { SECTION, SLOT, SLOT_PRESS } kind = Kind::SECTION;
	uint8_t index = 0;
};

/// @brief The board's map: which logical control @p input drives, or none.
/// @param board The board; a control past its sections or slots, or a press on a board with none, drives nothing.
/// @param input The physical control.
/// @return A board numbers its buttons and knobs as the layer numbers its sections and slots, so the logical control
///         has the physical one's index; the empty value for a control the board does not have.
[[nodiscard]] std::optional<Logical> map_input(const Board& board, Physical input);

} // namespace deluge::controls
