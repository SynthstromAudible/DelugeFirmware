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
#include "model/fx/effects_owner.h"

#include <optional>

/// The ModelStack owner slot's view of an owner: its effects, for code that holds only a ModelStack.
///
/// Instruments answer their own note tails (Voiced), expression events (Drum, MelodicInstrument) and CC output
/// (MIDIInstrument). This class goes with the slot when ModelStack is retired.
class ModControllable {
public:
	ModControllable() = default;
	virtual ~ModControllable() = default;
	/// @brief This owner's effects, for code that holds only the ModelStack's owner slot. A bridge: it goes with the
	/// slot when ModelStack is retired; an owner in a song is otherwise found by address.
	/// @return The owner's effects; nullopt for an owner without any (a MIDI or CV instrument).
	[[nodiscard]] virtual std::optional<deluge::model::EffectsOwner> effectsOwner() { return std::nullopt; }
};
